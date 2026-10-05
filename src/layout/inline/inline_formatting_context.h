// Local host for the standalone Blink inline layout and editing adapters.
#pragma once

#include <memory>
#include <unordered_map>

#include "base/heap_vector.h"
#include "base/vector.h"
#include "font/font_cache_client.h"
#include "layout/inline/fragment_items.h"
#include "paint/device_scale.h"
#include "text/text_break_iterator.h"
#include "text/writing_mode.h"

namespace bkfont {

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

struct InlineLayoutOptions {
  // Input whitespace is preserved, with break-spaces wrapping semantics.
  // Available space is already in layout/framebuffer pixels, like the
  // viewport supplied by WebFrameWidget. It is not multiplied by page zoom.
  LayoutUnit available_inline_size{640};
  WritingMode writing_mode = WritingMode::kHorizontalTb;
  TextDirection direction = TextDirection::kLtr;
  LineBreakType word_break = LineBreakType::kNormal;
  LineBreakStrictness line_break = LineBreakStrictness::kDefault;
  bool wrap = true;
  bool break_long_words = false;
  bool operator==(const InlineLayoutOptions&) const = default;
};

class InlineFormattingContext final : private FontCacheClient {
public:
  explicit InlineFormattingContext(const InlineStyle&, InlineLayoutOptions = {});
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
    return device_scale_;
  }
  float PageZoomFactor() const {
    return page_zoom_factor_;
  }
  float LayoutZoomFactor() const {
    return device_scale_.factor * page_zoom_factor_;
  }

  InlineLayoutState State() const {
    return epoch_->state;
  }
  uint64_t Generation() const {
    return epoch_->generation;
  }
  void UpdateLayout();
  // Geometry clients acquire a clean snapshot. Do not retain references across
  // mutations; use InlineCursor when a checked, temporary traversal is needed.
  const FragmentItems& Fragments();
  bool HasInlineFragments(const InlineObject&) const;
  // Geometry, hit testing and paint offsets all use layout/framebuffer pixels.
  PhysicalRect CaretRect(InlinePosition);
  PhysicalRect CaretRect(InlinePosition, LayoutUnit caret_width);
  InlinePosition HitTest(const PhysicalOffset&);
  Vector<PhysicalRect> SelectionRects(const InlineSelection&);
  // HighlightPainter adds the snapped text box origin before pixel snapping.
  // These rects include the paint offset; SelectionRects stays in layout space.
  Vector<PhysicalRect> SelectionRectsForPaint(const InlineSelection&, const PhysicalOffset& = {});
  Vector<PhysicalRect> ObjectRects(const InlineObject&);
  void Paint(PaintCanvas*, const PhysicalOffset& = {});

private:
  friend class InlineCursor;
  friend class InlineLayoutAlgorithm;
  friend class InlineEditor;
  void FontCacheInvalidated() override;
  InlineObject& Add(const InlineObject&, InlineObject::Type, std::shared_ptr<const InlineStyle>);
  void MarkDirty(const InlineObject&);
  void Retire(InlineObject&);
  void Validate(const InlineObject&) const;
  Vector<PhysicalRect> CollectSelectionRects(const InlineSelection&, const PhysicalOffset* paint_offset);
  std::unique_ptr<InlineObject> root_;
  std::unordered_map<InlineNodeId, InlineObject*> objects_;
  // Must outlive fragments_. Destruction also drops fragments before nodes.
  HeapVector<std::unique_ptr<InlineObject>> retired_;
  std::unique_ptr<FragmentItems> fragments_;
  std::shared_ptr<InlineLayoutEpoch> epoch_ = std::make_shared<InlineLayoutEpoch>();
  InlineLayoutOptions options_;
  DeviceScale device_scale_;
  float page_zoom_factor_ = 1;
  InlineNodeId next_id_ = 1;
};

} // namespace bkfont
