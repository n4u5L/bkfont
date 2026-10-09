// Local ownership and editing-geometry adapter for Blink's inline fragments.
#include "inline_formatting_context.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>

#include "base/notreached.h"
#include "base/text/string_builder.h"
#include "font/font_cache.h"
#include "font/font_selector.h"
#include "font/text_fragment_paint_info.h"
#include "layout/inline/inline_caret_position.h"
#include "layout/inline/inline_layout_algorithm.h"
#include "paint/text_fragment_painter.h"
#include "paint/text_decoration_info.h"

namespace bkit {

InlineFormattingContext::InlineFormattingContext(const InlineStyle& style, InlineLayoutOptions options)
    : InlineFormattingContext(Settings(),
                              style.font.GetFontSelector() ? style.font.GetFontSelector()->shared_from_this() : nullptr,
                              options) {
  root_->style_ = std::make_shared<const InlineStyle>(style);
}

InlineFormattingContext::InlineFormattingContext(const Settings& settings, std::shared_ptr<FontSelector> font_selector,
                                                 InlineLayoutOptions options)
    : options_(options), style_host_(settings, std::move(font_selector)),
      style_sheet_([this](const AtomicString& name) { RulesChanged(name); }) {
  options_.available_inline_size = options_.available_inline_size.ClampNegativeToZero();
  root_.reset(new InlineObject(*this, next_id_++, InlineObject::Type::kInline, nullptr));
  objects_.insert(root_->Id(), root_.get());
  FontCache::Get().AddClient(this);
}

InlineFormattingContext::~InlineFormattingContext() {
  epoch_.reset();
  fragments_.reset();
  retired_.clear();
}

void InlineFormattingContext::FontCacheInvalidated() {
  reuse_shape_results_ = false;
  // Device-scale changes may change hinting and advances even when the
  // logical viewport and styles are unchanged. No old line can be reused.
  MarkDirty(*root_);
  if (fragments_) fragments_->DirtyFirstItem();
  // lh/rlh specified values can depend on changed font metrics even though
  // the FontDescription and inherited computed line-height remain equal.
  style_host_.InvalidateInitialStyle();
  SetNeedsStyleRecalc(*root_, StyleChangeType::kSubtreeStyleChange);
}

const InlineObject* InlineFormattingContext::Find(InlineNodeId id) const {
  if (!objects_.IsValidKey(id)) return nullptr;
  const auto found = objects_.find(id);
  return found == objects_.end() ? nullptr : found->value;
}

void InlineFormattingContext::Validate(const InlineObject& object) const {
  assert(&object.Root() == this && object.IsAttached() && State() != InlineLayoutState::kInLayout);
}

// Tree changes, zoom and font data changes need new inline items. Style is
// marked separately, only for objects whose cascade inputs changed.
void InlineFormattingContext::MarkDirty(const InlineObject& object) {
  Validate(object);
  if (fragments_) fragments_->DirtyLinesFromChangedChild(object);
  epoch_->state = InlineLayoutState::kDirty;
  ++epoch_->generation;
  needs_collect_inlines_ = true;
  invalidation_.SetNeedsReshape();
}

InlineObject& InlineFormattingContext::Add(const InlineObject& parent, InlineObject::Type type,
                                           std::shared_ptr<const InlineStyle> style) {
  Validate(parent);
  assert(parent.IsInline());
  // Copy client-supplied styles so mutation through another shared_ptr cannot
  // silently alter a clean layout or an old snapshot.
  if (style) style = std::make_shared<const InlineStyle>(*style);
  auto child = std::unique_ptr<InlineObject>(new InlineObject(*this, next_id_++, type, std::move(style)));
  child->parent_ = const_cast<InlineObject*>(&parent);
  InlineObject& result = *child;
  objects_.insert(child->Id(), child.get());
  child->parent_->children_.push_back(std::move(child));
  MarkDirty(result);
  MarkAncestorsWithChildNeedsStyleRecalc(result);
  return result;
}

const InlineObject& InlineFormattingContext::AppendText(const InlineObject& parent, const String& text,
                                                        std::shared_ptr<const InlineStyle> style) {
  InlineObject& child = Add(parent, InlineObject::Type::kText, std::move(style));
  child.text_ = text;
  return child;
}

const InlineObject& InlineFormattingContext::AppendInline(const InlineObject& parent,
                                                          std::shared_ptr<const InlineStyle> style) {
  return Add(parent, InlineObject::Type::kInline, std::move(style));
}

const InlineObject& InlineFormattingContext::AppendAtomic(const InlineObject& parent, LogicalSize size,
                                                          LayoutUnit baseline, std::shared_ptr<const InlineStyle> style) {
  InlineObject& child = Add(parent, InlineObject::Type::kAtomic, std::move(style));
  child.atomic_size_ = {size.inline_size.ClampNegativeToZero(), size.block_size.ClampNegativeToZero()};
  child.atomic_baseline_ = baseline;
  return child;
}

void InlineFormattingContext::ReplaceText(const InlineObject& object, unsigned offset, unsigned length,
                                          const String& replacement) {
  Validate(object);
  assert(object.IsText() && offset <= object.Text().length() && length <= object.Text().length() - offset);
  if (!length && replacement.empty()) return;
  if (fragments_) fragments_->DirtyTextRange(object, offset);
  epoch_->state = InlineLayoutState::kDirty;
  ++epoch_->generation;
  needs_collect_inlines_ = true;
  invalidation_.SetNeedsReshape();
  StringBuilder builder;
  builder.Append(StringView(object.Text(), 0, offset));
  builder.Append(replacement);
  builder.Append(StringView(object.Text(), offset + length));
  const_cast<InlineObject&>(object).text_ = builder.ToString();
}

void InlineFormattingContext::SetStyle(const InlineObject& object, const InlineStyle& style) {
  Validate(object);
  const bool had_own_style = object.HasOwnStyle();
  const_cast<InlineObject&>(object).style_ = std::make_shared<const InlineStyle>(style);
  OwnStyleMayHaveChanged(object, had_own_style);
  SetNeedsStyleRecalc(object, StyleChangeType::kLocalStyleChange);
}

// A text object with its own style is an inline box for bidi: collecting
// wraps its text in bidi controls (InlineItemsBuilder::EnterInline()), so
// gaining or losing its own style needs new inline items.
void InlineFormattingContext::OwnStyleMayHaveChanged(const InlineObject& object, bool had_own_style) {
  if (object.IsText() && object.HasOwnStyle() != had_own_style) MarkDirty(object);
}

void InlineFormattingContext::SetNeedsStyleRecalc(const InlineObject& object, StyleChangeType type) {
  assert(epoch_->state != InlineLayoutState::kInLayout && !resolving_style_);
  auto& node = const_cast<InlineObject&>(object);
  node.style_change_ = std::max(node.style_change_, type);
  MarkAncestorsWithChildNeedsStyleRecalc(node);
  ++epoch_->generation;
}

// Node::MarkAncestorsWithChildNeedsStyleRecalc(). Stops at an ancestor that is
// already marked; Move() marks the moved object so this invariant holds.
void InlineFormattingContext::MarkAncestorsWithChildNeedsStyleRecalc(const InlineObject& object) {
  for (InlineObject* parent = object.parent_; parent && !parent->child_needs_style_recalc_; parent = parent->parent_)
    parent->child_needs_style_recalc_ = true;
  styles_dirty_ = true;
}

void InlineFormattingContext::RulesChanged(const AtomicString& name) {
  for (const auto& entry : objects_) {
    InlineObject* const object = entry.value;
    if (object->rules_.Contains(name))
      SetNeedsStyleRecalc(*object, StyleChangeType::kLocalStyleChange);
  }
}

void InlineFormattingContext::SetInlineStyle(const InlineObject& object, const StyleDeclaration& declaration) {
  Validate(object);
  if (object.declaration_ == declaration) return;
  const bool had_own_style = object.HasOwnStyle();
  const_cast<InlineObject&>(object).declaration_ = declaration;
  OwnStyleMayHaveChanged(object, had_own_style);
  SetNeedsStyleRecalc(object, StyleChangeType::kLocalStyleChange);
}

void InlineFormattingContext::SetRules(const InlineObject& object, Vector<AtomicString> rules) {
  Validate(object);
  if (object.rules_ == rules) return;
  const bool had_own_style = object.HasOwnStyle();
  const_cast<InlineObject&>(object).rules_ = std::move(rules);
  OwnStyleMayHaveChanged(object, had_own_style);
  SetNeedsStyleRecalc(object, StyleChangeType::kLocalStyleChange);
}

void InlineFormattingContext::SetSettings(const Settings& settings) {
  Validate(*root_);
  style_host_.SetSettings(settings);
  reuse_shape_results_ = false;
  SetNeedsStyleRecalc(*root_, StyleChangeType::kSubtreeStyleChange);
}

void InlineFormattingContext::SetFontSelector(std::shared_ptr<FontSelector> font_selector) {
  Validate(*root_);
  style_host_.SetFontSelector(std::move(font_selector));
  reuse_shape_results_ = false;
  SetNeedsStyleRecalc(*root_, StyleChangeType::kSubtreeStyleChange);
}

const ComputedStyle& InlineFormattingContext::ComputedStyleFor(const InlineObject& object) {
  Validate(object);
  UpdateStyle();
  return *object.computed_style_;
}

// Element::RecalcStyle() for the local tree. An object is resolved when it
// was marked or its parent's style changed; descendants are visited only
// through ChildNeedsStyleRecalc() or a propagated StyleRecalcChange.
void InlineFormattingContext::RecalcStyle(InlineObject& object, const ComputedStyle* parent, StyleRecalcChange change) {
  StyleRecalcChange child_change = change == StyleRecalcChange::kRecalcDescendants ||
                                           object.style_change_ == StyleChangeType::kSubtreeStyleChange
                                       ? StyleRecalcChange::kRecalcDescendants
                                       : StyleRecalcChange::kNone;
  bool recalc_explicit_inheritance = false;
  if (change != StyleRecalcChange::kNone || object.style_change_ != StyleChangeType::kNoStyleChange) {
    // ElementRuleCollector order: host defaults for the root as user-agent
    // declarations, then the named rules and the node's own block as author
    // declarations. They are declarations on the root, not initial values:
    // 'initial' and the non-inherited fields still use the initial style.
    MatchResult match_result;
    for (const auto& name : object.rules_)
      match_result.AddMatchedProperties(style_sheet_.Rule(name), CascadeOrigin::kAuthor);
    match_result.AddMatchedProperties(&object.declaration_, CascadeOrigin::kAuthor);
    StyleAdjustInput adjust;
    adjust.is_atomic_inline = object.IsAtomicInline();
    adjust.is_inline_content = parent && !object.IsAtomicInline();
    auto next = StyleResolver::Resolve(style_host_, parent, match_result, object.style_.get(), adjust);
    assert(next);
    StyleDifference diff;
    if (object.computed_style_) diff = object.computed_style_->VisualInvalidationDiff(*next);
    else diff.SetNeedsReshape();
    const bool whitespace_changed = object.computed_style_ &&
        (object.computed_style_->GetWhiteSpaceCollapse() != next->GetWhiteSpaceCollapse() ||
         object.computed_style_->GetTextWrapMode() != next->GetTextWrapMode());
    if (!object.computed_style_ || *object.computed_style_ != *next) {
      const bool inherited_changed = !object.computed_style_ || !object.computed_style_->InheritedEqual(*next);
      // Only root font metrics and line-height feed rem/rlh. A root color
      // change follows ordinary inheritance and stops at overriding nodes.
      const bool root_units_changed = !parent && (!object.computed_style_ ||
          *object.computed_style_->GetFont() != *next->GetFont() || object.computed_style_->LineHeight() != next->LineHeight());
      if (root_units_changed)
        child_change = StyleRecalcChange::kRecalcDescendants;
      else if (inherited_changed && child_change == StyleRecalcChange::kNone)
        child_change = StyleRecalcChange::kRecalcChildren;
      recalc_explicit_inheritance = !inherited_changed;
      object.computed_style_ = std::move(next);
      // No full fragment scan for a local style change. The per-object chain
      // includes generated text; root line styles are handled separately.
      if (fragments_ && !diff.NeedsLayout()) fragments_->RefreshStyle(object, object.computed_style_);
    }
    // Descendants resolve rem/rlh against the document element's style.
    if (!parent) style_host_.SetRootElementStyle(object.computed_style_);
    invalidation_.Merge(diff);
    if (diff.NeedsReshape()) {
      needs_collect_inlines_ = true;
      reuse_shape_results_ = false;
    }
    if (diff.NeedsLayout()) {
      if (fragments_) {
        // Leading spaces and new break opportunities can change the preceding
        // line even when this object's first fragment is on the next line.
        if (whitespace_changed) fragments_->DirtyTextRange(object, 0);
        else fragments_->DirtyLinesFromChangedChild(object);
      }
      epoch_->state = InlineLayoutState::kDirty;
    }
  }
  object.style_change_ = StyleChangeType::kNoStyleChange;
  const bool child_needs_style_recalc = object.child_needs_style_recalc_;
  object.child_needs_style_recalc_ = false;
  if (child_change == StyleRecalcChange::kNone && !child_needs_style_recalc && !recalc_explicit_inheritance) return;
  for (auto& child : object.children_) {
    const StyleRecalcChange change_for_child = child_change == StyleRecalcChange::kNone && recalc_explicit_inheritance &&
                                                child->computed_style_ && child->computed_style_->HasExplicitInheritance()
                                            ? StyleRecalcChange::kRecalcChildren : child_change;
    if (change_for_child != StyleRecalcChange::kNone || child->style_change_ != StyleChangeType::kNoStyleChange ||
        child->child_needs_style_recalc_)
      RecalcStyle(*child, object.computed_style_.get(), change_for_child);
  }
}

void InlineFormattingContext::UpdateStyle() {
  if (!styles_dirty_) return;
  assert(epoch_->state != InlineLayoutState::kInLayout && !resolving_style_);
  FontCache::UpdateDeviceScaleFactor(style_host_.DeviceScaleFactor());
  resolving_style_ = true;
  RecalcStyle(*root_, nullptr, StyleRecalcChange::kNone);
  styles_dirty_ = false;
  resolving_style_ = false;
}

StyleDifference InlineFormattingContext::TakeInvalidation() {
  UpdateStyle();
  const StyleDifference result = invalidation_;
  invalidation_ = {};
  return result;
}

void InlineFormattingContext::Retire(InlineObject& object) {
  object.attached_ = false;
  objects_.erase(object.Id());
  for (auto& child : object.children_) Retire(*child);
}

void InlineFormattingContext::Remove(const InlineObject& object) {
  Validate(object);
  assert(object.parent_);
  MarkDirty(object);
  auto& children = object.parent_->children_;
  const auto it = std::find_if(children.begin(), children.end(), [&](const auto& child) { return child.get() == &object; });
  Retire(**it);
  // Preserve the detached subtree's structure while old fragments reference it.
  retired_.push_back(std::move(*it));
  children.erase(it);
}

bool InlineFormattingContext::Move(const InlineObject& object, const InlineObject& parent, const InlineObject* before) {
  Validate(object);
  Validate(parent);
  if (!object.parent_ || !parent.IsInline() || &object == &parent || parent.IsDescendantOf(object) ||
      (before && before->Parent() != &parent)) return false;
  if (before == &object) return true;
  MarkDirty(object);
  auto& source = object.parent_->children_;
  const auto from = std::find_if(source.begin(), source.end(), [&](const auto& child) { return child.get() == &object; });
  auto moving = std::move(*from);
  source.erase(from);
  auto& destination = const_cast<InlineObject&>(parent).children_;
  const auto to = before ? std::find_if(destination.begin(), destination.end(),
                                        [&](const auto& child) { return child.get() == before; })
                         : destination.end();
  moving->parent_ = const_cast<InlineObject*>(&parent);
  destination.insert(static_cast<wtf_size_t>(to - destination.begin()), std::move(moving));
  // A move can change bidi context and inherited style before either endpoint.
  if (fragments_) fragments_->DirtyFirstItem();
  SetNeedsStyleRecalc(object, StyleChangeType::kLocalStyleChange);
  return true;
}

void InlineFormattingContext::SetOptions(InlineLayoutOptions options) {
  options.available_inline_size = options.available_inline_size.ClampNegativeToZero();
  if (options_ == options) return;
  Validate(*root_);
  options_ = options;
  if (fragments_) {
    fragments_->DirtyLinesFromChangedChild(*root_);
    fragments_->DirtyFirstItem();
  }
  epoch_->state = InlineLayoutState::kDirty;
  ++epoch_->generation;
  invalidation_.SetNeedsFullLayout();
}

bool InlineFormattingContext::SetZoomFactors(float device_scale_factor, float page_zoom_factor) {
  if (!StyleHostContext::IsValidZoom(device_scale_factor, page_zoom_factor)) return false;
  Validate(*root_);
  // Recreates the initial style when a factor changed.
  const bool changed = style_host_.SetZoomFactors(device_scale_factor, page_zoom_factor);
  // Font render-style selection uses DSF alone, not page zoom. The computed
  // font size uses their product, as in WebFrameWidget/LocalFrame/FontBuilder.
  const bool fonts_changed = FontCache::UpdateDeviceScaleFactor(device_scale_factor);
  if (changed || fonts_changed) reuse_shape_results_ = false;
  if (changed) {
    MarkDirty(*root_);
    if (fragments_) fragments_->DirtyFirstItem();
    SetNeedsStyleRecalc(*root_, StyleChangeType::kSubtreeStyleChange);
  }
  return changed || fonts_changed;
}

void InlineFormattingContext::UpdateLayout() {
  assert(State() != InlineLayoutState::kInLayout);
  // LayoutView::LayoutRoot activates its Page's DSF before font lookup. Other
  // contexts may have used this thread since the last visual-properties update.
  FontCache::UpdateDeviceScaleFactor(style_host_.DeviceScaleFactor());
  UpdateStyle();
  if (State() == InlineLayoutState::kClean) return;
  epoch_->state = InlineLayoutState::kInLayout;
  auto next = InlineLayoutAlgorithm(*this).Layout();
  // Finalize has already rebuilt first-item indices, deltas and fragment ids.
  // Destroy the old index AND items before any address can be recycled.
  fragments_ = std::move(next);
  retired_.clear();
  needs_collect_inlines_ = false;
  reuse_shape_results_ = true;
  ++epoch_->generation;
  ++layout_generation_;
  epoch_->state = InlineLayoutState::kClean;
}

const FragmentItems& InlineFormattingContext::Fragments() {
  UpdateLayout();
  return *fragments_;
}

bool InlineFormattingContext::HasInlineFragments(const InlineObject& object) const {
  return State() == InlineLayoutState::kClean && fragments_ &&
         fragments_->FirstInlineFragmentItemIndex(object) != 0;
}

namespace {

// FragmentItem::LocalRect() and the LTR WritingModeConverter used by
// ComputeLogicalCaretRectAtTextOffset(). Positions are relative to the item.
PhysicalRect InlineRangeRect(const FragmentItem& item,
                             LayoutUnit start_position, LayoutUnit end_position, WritingMode writing_mode) {
  const auto& rect = item.RectInContainerFragment();
  const LayoutUnit inline_size = end_position - start_position;
  if (IsHorizontalWritingMode(writing_mode)) return {rect.X() + start_position, rect.Y(), inline_size, rect.Height()};
  const LayoutUnit top = writing_mode == WritingMode::kSidewaysLr ? rect.Bottom() - end_position : rect.Y() + start_position;
  return {rect.X(), top, rect.Width(), inline_size};
}

// InlineCursor::ExpandSelectionRectToLineHeight() takes the union in the
// block direction; text can extend outside a line with negative leading.
PhysicalRect ExpandSelectionRectToLineHeight(PhysicalRect rect, const FragmentItem& line,
                                             WritingMode writing_mode) {
  const auto& line_rect = line.RectInContainerFragment();
  if (IsHorizontalWritingMode(writing_mode)) {
    const LayoutUnit top = std::min(rect.Y(), line_rect.Y());
    const LayoutUnit bottom = std::max(rect.Bottom(), line_rect.Bottom());
    rect.SetY(top);
    rect.SetHeight(bottom - top);
  } else {
    const LayoutUnit left = std::min(rect.X(), line_rect.X());
    const LayoutUnit right = std::max(rect.Right(), line_rect.Right());
    rect.SetX(left);
    rect.SetWidth(right - left);
  }
  return rect;
}

// FragmentItem::LineLeftAndRightForOffsets(): the unrounded caret positions
// are snapped outwards, then ordered.
std::pair<LayoutUnit, LayoutUnit> LineLeftAndRightForPositions(float unrounded_start_position,
                                                               float unrounded_end_position) {
  LayoutUnit start_position;
  LayoutUnit end_position;
  if (unrounded_start_position > unrounded_end_position) [[unlikely]] {
    start_position = LayoutUnit::FromFloatCeil(unrounded_start_position);
    end_position = LayoutUnit::FromFloatFloor(unrounded_end_position);
  } else {
    start_position = LayoutUnit::FromFloatFloor(unrounded_start_position);
    end_position = LayoutUnit::FromFloatCeil(unrounded_end_position);
  }

  // Swap positions if RTL.
  if (start_position > end_position) [[unlikely]] {
    return std::make_pair(end_position, start_position);
  }
  return std::make_pair(start_position, end_position);
}

// From caret_rect.cc.
bool ShouldAlignCaretRight(ETextAlign text_align, TextDirection direction) {
  switch (text_align) {
    case ETextAlign::kRight:
    case ETextAlign::kWebkitRight:
      return true;
    case ETextAlign::kLeft:
    case ETextAlign::kWebkitLeft:
    case ETextAlign::kCenter:
    case ETextAlign::kWebkitCenter:
      return false;
    case ETextAlign::kJustify:
    case ETextAlign::kStart:
      return IsRtl(direction);
    case ETextAlign::kEnd:
      return IsLtr(direction);
  }
  NOTREACHED();
}

LayoutUnit ClampAndRound(LayoutUnit value, LayoutUnit min, LayoutUnit max) {
  LayoutUnit min_ceil = LayoutUnit(min.Ceil());
  LayoutUnit max_floor = LayoutUnit(max.Floor());
  if (min_ceil >= max_floor) return max_floor;
  return LayoutUnit(std::clamp(value, min_ceil, max_floor).Round());
}

} // namespace

PhysicalRect InlineFormattingContext::CaretRect(InlinePosition position) {
  // LocalFrameView::BarCaretWidth: one DIP, at least one framebuffer pixel.
  // Page zoom changes text size but does not enlarge the insertion bar.
  return CaretRect(position, LayoutUnit(std::max(1.f, style_host_.DeviceScaleFactor())));
}

PhysicalRect InlineFormattingContext::CaretRect(InlinePosition position, LayoutUnit caret_width) {
  const auto caret = ComputeInlineCaretPosition(*this, position);
  if (!caret) return {};
  const auto& item = *caret.cursor.Current();
  if (item.Style().Visibility() != EVisibility::kVisible) return {};
  const auto& line = (*fragments_)[item.LineIndex()];
  if (caret.position_type == InlineCaretPositionType::kBeforeBox ||
      caret.position_type == InlineCaretPositionType::kAfterBox) {
    // ComputeLocalCaretRectByBoxSide(): the far edge is inset by the caret
    // width. The bar spans the line, without text-caret clamping or rounding.
    const bool before = caret.position_type == InlineCaretPositionType::kBeforeBox;
    const LayoutUnit caret_left = IsLtr(item.ResolvedDirection()) != before
                                      ? item.InlineSize() - caret_width
                                      : LayoutUnit();
    PhysicalRect rect = InlineRangeRect(item, caret_left, caret_left + caret_width, fragments_->GetWritingMode());
    const auto& line_rect = line.RectInContainerFragment();
    if (IsHorizontalWritingMode(fragments_->GetWritingMode())) {
      rect.SetY(line_rect.Y());
      rect.SetHeight(line_rect.Height());
    } else {
      rect.SetX(line_rect.X());
      rect.SetWidth(line_rect.Width());
    }
    return rect;
  }
  // FragmentItem::CaretInlinePositionForOffset() rounds to a LayoutUnit.
  LayoutUnit caret_left = LayoutUnit::FromFloatRound(item.CaretInlinePosition(caret.text_offset, fragments_->TextContent()));
  if (caret.position_type == InlineCaretPositionType::kAtTextOffset && !item.IsLineBreak()) caret_left -= caret_width / 2;
  // Text carets use the text fragment's block size and offset, not line-height.
  PhysicalRect rect = InlineRangeRect(item, caret_left, caret_left + caret_width, fragments_->GetWritingMode());

  // ComputeLocalCaretRectAtTextOffset(): adjust the location to ensure that
  // it completely falls in the union of line box and containing block, and
  // then round it to the nearest pixel.
  const PhysicalRect& line_box_rect = line.RectInContainerFragment();
  const PhysicalSize fragment_size = fragments_->SizeInPhysicalCoordinates();
  if (IsHorizontalWritingMode(fragments_->GetWritingMode())) {
    // A forced break or the end of the content uses text-align-last, as in
    // caret_rect.cc's absent/forced InlineBreakToken check.
    const bool is_last_line = !line.HasSoftWrapToNextLine();
    if (ShouldAlignCaretRight(line.Style().GetTextAlign(is_last_line), line.ResolvedDirection())) {
      const LayoutUnit left_edge = std::min(LayoutUnit(), line_box_rect.X());
      const LayoutUnit right_limit = line_box_rect.Right() - caret_width;
      rect.offset.left = ClampAndRound(rect.offset.left, left_edge, right_limit);
    } else {
      const LayoutUnit right_limit = std::max(fragment_size.width, line_box_rect.Right()) - caret_width;
      rect.offset.left = ClampAndRound(rect.offset.left, line_box_rect.X(), right_limit);
    }
    return rect;
  }

  // Similar adjustment and rounding for vertical text.
  const LayoutUnit min_y = std::min(LayoutUnit(), line_box_rect.Y());
  const LayoutUnit bottom_limit = std::max(fragment_size.height, line_box_rect.Bottom()) - caret_width;
  rect.offset.top = ClampAndRound(rect.offset.top, min_y, bottom_limit);
  return rect;
}

InlinePosition InlineFormattingContext::HitTest(const PhysicalOffset& point, InlineHitTestOptions options) {
  InlineCursor cursor(*this);
  if (const auto position = cursor.PositionForPointInInlineFormattingContext(point, options)) return position;
  // PhysicalBoxFragment::PositionForPoint falls back to its container's start
  // when the inline cursor cannot resolve a position, including empty blocks.
  return {root_->Id(), 0};
}

Vector<PhysicalRect> InlineFormattingContext::SelectionRects(const InlineSelection& selection) {
  return CollectSelectionRects(selection, nullptr);
}

Vector<PhysicalRect> InlineFormattingContext::SelectionRectsForPaint(const InlineSelection& selection,
                                                                    const PhysicalOffset& paint_offset) {
  return CollectSelectionRects(selection, &paint_offset);
}

Vector<PhysicalRect> InlineFormattingContext::CollectSelectionRects(const InlineSelection& selection,
                                                                   const PhysicalOffset* paint_offset) {
  const auto& fragments = Fragments();
  const auto anchor = fragments.Mapping().GetTextContentOffset(selection.anchor);
  const auto focus = fragments.Mapping().GetTextContentOffset(selection.focus);
  Vector<PhysicalRect> rects;
  if (!anchor || !focus || *anchor == *focus) return rects;
  const unsigned start = std::min(*anchor, *focus);
  const unsigned end = std::max(*anchor, *focus);
  const auto append_rect = [&](const FragmentItem& item, const FragmentItem& line, PhysicalRect rect) {
    rect = ExpandSelectionRectToLineHeight(rect, line, fragments_->GetWritingMode());
    if (paint_offset && item.IsText()) {
      // HighlightPainter::SelectionPaintState::ComputeSelectionRectIfNeeded
      // adds PhysicalBoxRect's origin, including its line-relative y rounding.
      const auto& item_rect = item.RectInContainerFragment();
      const auto box = PhysicalBoxRect(item_rect, *paint_offset, line.RectInContainerFragment().offset, nullptr);
      rect.Move(box.offset - item_rect.offset);
    } else if (paint_offset) {
      rect.Move(*paint_offset);
    }
    rects.push_back(rect);
  };
  for (const auto& item : fragments.Items()) {
    if (item.Type() == FragmentItem::kLine) continue;
    // TextFragmentPainter::Paint (and BoxFragmentPainter for boxes) returns
    // before painting the selection of a hidden item; report no highlight.
    if (item.Style().Visibility() != EVisibility::kVisible) continue;
    const auto& line = fragments[item.LineIndex()];
    const unsigned from = std::max(start, item.TextOffset().start);
    const unsigned to = std::min(end, item.TextOffset().end);
    if (item.IsGeneratedText() && start < item.TextOffset().start && end >= item.TextOffset().start) {
      append_rect(item, line, item.RectInContainerFragment());
      continue;
    }
    // MoveToLastLogicalLeaf uses the visual last/first leaf for LTR/RTL.
    const size_t last_leaf_index = item.LineIndex() +
                                  (IsLtr(line.ResolvedDirection()) ? line.DescendantsCount() - 1 : 1);
    const bool selected_soft_wrap = item.IsText() && !item.IsGeneratedText() && !item.IsLineBreak() &&
                                    line.HasSoftWrapToNextLine() && &item == &fragments[last_leaf_index] &&
                                    start <= item.TextOffset().end && end > item.TextOffset().end &&
                                    item.ResolvedDirection() == line.ResolvedDirection();
    if (item.IsGeneratedText() || from > to || (from == to && !selected_soft_wrap)) continue;
    LayoutUnit start_position;
    LayoutUnit end_position = item.InlineSize();
    // FragmentItem::LocalRect() covers the whole item without measuring.
    if (item.IsLineBreak()) {
      // CurrentLocalSelectionRectForText(): preserved newlines get a space
      // width. The last-<br> exception does not apply to this text-only model.
      end_position = LayoutUnit(item.Style().GetFont()->SpaceWidth());
      if (IsRtl(item.ResolvedDirection())) {
        start_position = -end_position;
        end_position = LayoutUnit();
      }
    } else if (from != item.TextOffset().start || to != item.TextOffset().end) {
      // LineLeftAndRightForOffsets expands partial grapheme selections at
      // both ends, before rounding outwards to LayoutUnit precision.
      const float begin = item.CaretInlinePosition(from, fragments.TextContent(), AdjustMidCluster::kToStart);
      const float finish = item.CaretInlinePosition(to, fragments.TextContent(), AdjustMidCluster::kToEnd);
      const auto edges = LineLeftAndRightForPositions(begin, finish);
      start_position = edges.first;
      end_position = edges.second;
    }
    // IsBeforeSoftLineBreak/ExpandedSelectionRectForSoftLineBreakIfNeeded:
    // extend the last logical text leaf only when the selection crosses the
    // wrap and its direction agrees with the line's base direction.
    if (selected_soft_wrap) {
      const LayoutUnit space_width(item.Style().GetFont()->SpaceWidth());
      if (IsLtr(item.ResolvedDirection()))
        end_position += space_width;
      else
        start_position -= space_width;
    }
    append_rect(item, line, InlineRangeRect(item, start_position, end_position, fragments_->GetWritingMode()));
  }
  return rects;
}

Vector<PhysicalRect> InlineFormattingContext::ObjectRects(const InlineObject& object) {
  Vector<PhysicalRect> rects;
  InlineCursor cursor(*this);
  cursor.MoveToIncludingCulledInline(object);
  for (; cursor; cursor.MoveToNextForSameLayoutObject()) rects.push_back(cursor.Current()->RectInContainerFragment());
  return rects;
}

void InlineFormattingContext::Paint(PaintCanvas* canvas, const PhysicalOffset& offset) {
  if (!canvas) return;
  const auto& fragments = Fragments();
  for (const auto& item : fragments.Items()) {
    if (const TextCombine* text_combine = item.GetTextCombine()) {
      // BoxFragmentPainter::PaintInternal() for LayoutTextCombine.
      if (item.Style().Visibility() == EVisibility::kVisible) {
        PaintTextCombine(canvas, *text_combine, item.RectInContainerFragment().offset + offset, item.Style());
      }
      continue;
    }
    if (!item.IsText() || item.IsLineBreak() || !item.TextShapeResult() ||
        item.Style().Visibility() != EVisibility::kVisible) continue;
    // BoxFragmentPainter::PaintLineBoxChildItems passes the line box offset
    // as the parent offset of its children.
    const PhysicalRect box = PhysicalBoxRect(item.RectInContainerFragment(), offset,
                                             fragments[item.LineIndex()].RectInContainerFragment().offset, nullptr);
    const auto range = item.TextOffset();
    const TextFragmentPaintInfo info{item.IsGeneratedText() ? StringView(item.GeneratedText()) : StringView(fragments.TextContent()),
                                     item.IsGeneratedText() ? 0 : range.start, item.IsGeneratedText() ? item.GeneratedText().length() : range.end, item.TextShapeResult()};
    Vector<DecoratingBox> decorating_boxes;
    if (IsHorizontalWritingMode(fragments.GetWritingMode()) && item.Style().HasAppliedTextDecorations()) {
      // TextDecorationInfo::OffsetFromDecoratingBox() adds the unrounded
      // paint offset (InlinePaintContext::ScopedPaintOffset), not the
      // rounded line top of PhysicalBoxRect().
      for (const auto& paint_box : item.PaintBoxes()) {
        const ComputedStyle& decorating_style = paint_box.object->Style();
        const auto count = decorating_style.AppliedTextDecorations().size();
        if (count < decorating_boxes.size()) decorating_boxes.resize(count);
        while (decorating_boxes.size() < count) {
          decorating_boxes.push_back(DecoratingBox{&decorating_style, (paint_box.block_offset + offset.top).ToFloat()});
        }
      }
    }
    PaintTextFragment(canvas, info, *item.Style().GetFont(), fragments.GetWritingMode(), box, item.Style(),
                       kInvalidNodeId, decorating_boxes);
  }
}

} // namespace bkit
