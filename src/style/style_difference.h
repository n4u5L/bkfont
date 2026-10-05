// Standalone subset of core/style/style_difference.h. Flags compose; a caller
// must not infer layout invalidation from pointer identity or paint changes.
// The current inline-property subset emits reshape/layout/paint. Visual
// overflow and layout-tree reattachment are reserved for the box/decoration
// ports; exposing their bits does not mean those layout features exist yet.
#pragma once

namespace bkfont {

class StyleDifference {
public:
  void Merge(StyleDifference other) { flags_ |= other.flags_; }
  bool HasDifference() const { return flags_ != 0; }
  bool NeedsReshape() const { return flags_ & kReshape; }
  bool NeedsLayout() const { return flags_ & kLayout; }
  bool NeedsFullLayout() const { return NeedsLayout(); }
  bool NeedsNormalPaintInvalidation() const { return flags_ & kPaint; }
  bool NeedsRecomputeVisualOverflow() const { return flags_ & kVisualOverflow; }
  bool NeedsReattachLayoutTree() const { return flags_ & kReattach; }
  void SetNeedsReshape() { flags_ |= kReshape | kLayout | kPaint; }
  void SetNeedsFullLayout() { flags_ |= kLayout | kPaint; }
  void SetNeedsNormalPaintInvalidation() { flags_ |= kPaint; }
  void SetNeedsRecomputeVisualOverflow() { flags_ |= kVisualOverflow | kPaint; }
  void SetNeedsReattachLayoutTree() { flags_ |= kReattach | kLayout | kPaint; }

private:
  enum { kReshape = 1, kLayout = 2, kPaint = 4, kVisualOverflow = 8, kReattach = 16 };
  unsigned flags_ = 0;
};

} // namespace bkfont
