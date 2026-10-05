// Local model adapter for Blink's LayoutText/LayoutInline.
#include "inline_object.h"

#include <cassert>
#include <cmath>

#include "font/font_selector.h"
#include "layout/inline/inline_formatting_context.h"

namespace bkfont {

InlineStyle InlineStyle::Zoom(float factor) const {
  assert(std::isfinite(factor) && factor > 0);
  InlineStyle result(*this);
  if (factor == 1) return result;
  auto description = font.GetFontDescription();
  description.SetComputedSize(description.ComputedSize() * factor);
  description.SetAdjustedSize(description.AdjustedSize() * factor);
  description.SetLetterSpacing(description.ComputedLetterSpacing().Zoom(factor));
  description.SetWordSpacing(description.ComputedWordSpacing().Zoom(factor));
  auto* selector = font.GetFontSelector();
  result.font = Font(description, selector ? selector->shared_from_this() : nullptr);
  result.line_height = line_height.Zoom(factor);
  if (!tab_size.IsSpaces()) result.tab_size.float_value_ *= factor;
  result.baseline_shift *= factor;
  return result;
}

InlineObject::InlineObject(InlineFormattingContext& root, InlineNodeId id, Type type,
                           std::shared_ptr<const InlineStyle> style)
    : root_(&root),
      id_(id),
      type_(type),
      style_(std::move(style)) {
}

const InlineObject* InlineObject::PreviousSibling() const {
  if (!parent_) return nullptr;
  const InlineObject* previous = nullptr;
  for (const auto& child : parent_->children_) {
    if (child.get() == this) return previous;
    previous = child.get();
  }
  return nullptr;
}

const InlineObject* InlineObject::NextSibling() const {
  if (!parent_) return nullptr;
  bool found = false;
  for (const auto& child : parent_->children_) {
    if (found) return child.get();
    found = child.get() == this;
  }
  return nullptr;
}

const InlineObject* InlineObject::FirstChild() const {
  return children_.empty() ? nullptr : children_.front().get();
}

const InlineObject* InlineObject::LastChild() const {
  return children_.empty() ? nullptr : children_.back().get();
}

bool InlineObject::IsDescendantOf(const InlineObject& ancestor) const {
  for (const InlineObject* parent = parent_; parent; parent = parent->parent_)
    if (parent == &ancestor) return true;
  return false;
}

const InlineStyle& InlineObject::Style() const {
  return style_ ? *style_ : parent_->Style();
}

const std::shared_ptr<const InlineStyle>& InlineObject::LayoutStyle() const {
  if (!style_) return parent_->LayoutStyle();
  const float zoom = root_->LayoutZoomFactor();
  if (zoom == 1) return style_;
  if (layout_style_source_ != style_ || layout_style_zoom_ != zoom) {
    layout_style_ = std::make_shared<const InlineStyle>(style_->Zoom(zoom));
    layout_style_source_ = style_;
    layout_style_zoom_ = zoom;
  }
  return layout_style_;
}

LogicalSize InlineObject::LayoutAtomicSize() const {
  return atomic_size_ * root_->LayoutZoomFactor();
}

LayoutUnit InlineObject::LayoutAtomicBaseline() const {
  return LayoutUnit(atomic_baseline_ * root_->LayoutZoomFactor());
}

bool InlineObject::HasInlineFragments() const {
  return root_->HasInlineFragments(*this);
}

} // namespace bkfont
