// Local host for the standalone Blink inline layout and editing adapters.
#pragma once

#include <memory>

#include "base/hash_map.h"
#include "base/heap_vector.h"
#include "base/vector.h"
#include "build/build_config.h"
#include "font/font_cache_client.h"
#include "layout/inline/fragment_items.h"
#include "paint/device_scale.h"
#include "text/text_break_iterator.h"
#include "text/writing_mode.h"
#include "style/style_host_context.h"
#include "style/style_resolver.h"
#include "style/style_sheet.h"

namespace bkit {

class PaintCanvas;
class InlineCursor;
class InlineLayoutAlgorithm;

enum class InlineLayoutState {
  kClean,
  kDirty,
  kInLayout
};

// Separate allocation lets a cursor reject a destroyed root before touching it.
struct InlineLayoutEpoch {
  uint64_t generation = 0;
  InlineLayoutState state = InlineLayoutState::kDirty;
};

// The block container's layout inputs that are not CSS. Its writing mode,
// direction, unicode-bidi, line breaking, alignment and indentation are the
// computed style of the root object.
struct InlineLayoutOptions {
  // Available space is already in layout/framebuffer pixels, like the
  // viewport supplied by WebFrameWidget. It is not multiplied by page zoom.
  LayoutUnit available_inline_size{640};
  bool operator==(const InlineLayoutOptions&) const = default;
};

struct InlineHitTestOptions {
  // EditingBehavior::ShouldMoveCaretToHorizontalBoundaryWhenPastTopOrBottom:
  // Windows preserves the inline position; Unix moves to a line boundary.
  bool move_caret_to_horizontal_boundary_when_past_top_or_bottom = !BUILDFLAG(IS_WIN);
};

class InlineFormattingContext final : private FontCacheClient {
public:
  explicit InlineFormattingContext(const InlineStyle&, InlineLayoutOptions = {});
  // The InlineStyle constructor takes the font selector of `style.font`.
  InlineFormattingContext(const Settings&, std::shared_ptr<FontSelector>, InlineLayoutOptions = {});
  ~InlineFormattingContext() override;
  InlineFormattingContext(const InlineFormattingContext&) = delete;
  InlineFormattingContext& operator=(const InlineFormattingContext&) = delete;

  const InlineObject& RootObject() const {
    return *root_;
  }
  const InlineObject* Find(InlineNodeId) const;
  const InlineObject& AppendText(const InlineObject& parent, const String&,
                                 std::shared_ptr<const InlineStyle> = nullptr);
  const InlineObject& AppendInline(const InlineObject& parent,
                                   std::shared_ptr<const InlineStyle> = nullptr);
  const InlineObject& AppendAtomic(const InlineObject& parent, LogicalSize, LayoutUnit baseline,
                                   std::shared_ptr<const InlineStyle> = nullptr);
  void ReplaceText(const InlineObject&, unsigned offset, unsigned length, const String&);
  void SetStyle(const InlineObject&, const InlineStyle&);
  void SetInlineStyle(const InlineObject&, const StyleDeclaration&);
  void SetRules(const InlineObject&, Vector<AtomicString>);
  bkit::StyleSheet& StyleSheet() { return style_sheet_; }
  const bkit::StyleSheet& StyleSheet() const { return style_sheet_; }
  // Document-level style inputs. Changing either recalculates every style.
  const StyleHostContext& StyleHost() const {
    return style_host_;
  }
  void SetSettings(const Settings&);
  void SetFontSelector(std::shared_ptr<FontSelector>);
  const ComputedStyle& ComputedStyleFor(const InlineObject&);
  // Recalculates only objects marked by a mutation and the descendants that
  // inherit from a changed style (Document::UpdateStyleAndLayoutTree).
  void UpdateStyle();
  // The work needed by every mutation since the previous call: tree and text
  // edits, options, zoom and font-cache changes, and style differences.
  // Obtaining geometry or painting does not consume this notification.
  StyleDifference TakeInvalidation();
  void Remove(const InlineObject&);
  // Same-root moves; before == nullptr appends. Old objects remain allocated.
  bool Move(const InlineObject&, const InlineObject& parent, const InlineObject* before = nullptr);
  void SetOptions(InlineLayoutOptions);
  const InlineLayoutOptions& Options() const {
    return options_;
  }
  // Host entry corresponding to WebView/LocalFrame zoom propagation. Update
  // before input or painting after a display change, on the font/layout thread.
  // Invalid factors leave both the context and font cache unchanged.
  bool SetZoomFactors(float device_scale_factor, float page_zoom_factor = 1);
  const DeviceScale& GetDeviceScale() const {
    return style_host_.GetDeviceScale();
  }
  float PageZoomFactor() const {
    return style_host_.PageZoomFactor();
  }
  float LayoutZoomFactor() const {
    return style_host_.LayoutZoomFactor();
  }

  InlineLayoutState State() const {
    return styles_dirty_ && epoch_->state == InlineLayoutState::kClean ? InlineLayoutState::kDirty : epoch_->state;
  }
  uint64_t Generation() const {
    return epoch_->generation;
  }
  uint64_t LayoutGeneration() const { return layout_generation_; }
  void UpdateLayout();
  // Geometry clients acquire a clean snapshot. Do not retain references across
  // mutations or UpdateStyle/UpdateLayout calls; use InlineCursor when a
  // checked, temporary traversal is needed.
  const FragmentItems& Fragments();
  bool HasInlineFragments(const InlineObject&) const;
  // Geometry, hit testing and paint offsets all use layout/framebuffer pixels.
  PhysicalRect CaretRect(InlinePosition);
  PhysicalRect CaretRect(InlinePosition, LayoutUnit caret_width);
  // PositionForPoint in this formatting context. Generated text and empty
  // lines are skipped; if no position resolves, returns the root's start.
  // Like Blink's caret-position lookup, this is not a painted-node hit test
  // and does not exclude visibility:hidden text.
  InlinePosition HitTest(const PhysicalOffset&, InlineHitTestOptions = {});
  Vector<PhysicalRect> SelectionRects(const InlineSelection&);
  // HighlightPainter adds the snapped text box origin before pixel snapping.
  // These rects include the paint offset; SelectionRects stays in layout space.
  Vector<PhysicalRect> SelectionRectsForPaint(const InlineSelection&, const PhysicalOffset& = {});
  Vector<PhysicalRect> ObjectRects(const InlineObject&);
  void Paint(PaintCanvas*, const PhysicalOffset& = {});

private:
  friend class InlineCursor;
  friend class InlineLayoutAlgorithm;
  void FontCacheInvalidated() override;
  InlineObject& Add(const InlineObject&, InlineObject::Type, std::shared_ptr<const InlineStyle>);
  // StyleRecalcChange: kRecalcChildren covers direct children only.
  enum class StyleRecalcChange {
    kNone,
    kRecalcChildren,
    kRecalcDescendants
  };
  void MarkDirty(const InlineObject&);
  void OwnStyleMayHaveChanged(const InlineObject&, bool had_own_style);
  void SetNeedsStyleRecalc(const InlineObject&, StyleChangeType);
  void MarkAncestorsWithChildNeedsStyleRecalc(const InlineObject&);
  void RulesChanged(const AtomicString& name);
  void RecalcStyle(InlineObject&, const ComputedStyle* parent, StyleRecalcChange);
  void Retire(InlineObject&);
  void Validate(const InlineObject&) const;
  Vector<PhysicalRect> CollectSelectionRects(const InlineSelection&, const PhysicalOffset* paint_offset);
  std::unique_ptr<InlineObject> root_;
  // Attached objects by ID. IDs start at 1, so 0 is never a key.
  HashMap<InlineNodeId, InlineObject*> objects_;
  // Must outlive fragments_. Destruction also drops fragments before nodes.
  HeapVector<std::unique_ptr<InlineObject>> retired_;
  std::unique_ptr<FragmentItems> fragments_;
  std::shared_ptr<InlineLayoutEpoch> epoch_ = std::make_shared<InlineLayoutEpoch>();
  InlineLayoutOptions options_;
  // Settings, font selector, root style and zoom factors. Root declarations
  // do not affect the cached initial style.
  StyleHostContext style_host_;
  bkit::StyleSheet style_sheet_;
  StyleDifference invalidation_;
  // The root needs or has a descendant needing style recalc.
  bool styles_dirty_ = true;
  bool resolving_style_ = false;
  // InlineNode::NeedsCollectInlines(): text, tree, base direction, wrapping,
  // zoom, font data or a reshaping style change. Otherwise reuse shape results.
  bool needs_collect_inlines_ = true;
  // The previous layout's shape results stay valid for the text an edit left
  // unchanged (ReusingTextShaper). Zoom, font data, settings and any style
  // change that needs reshaping, including the style of a new object, clear
  // it; a layout sets it.
  bool reuse_shape_results_ = false;
  uint64_t layout_generation_ = 0;
  InlineNodeId next_id_ = 1;
};

} // namespace bkit
