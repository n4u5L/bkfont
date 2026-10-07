// Subset of: blink/renderer/core/layout/inline/inline_box_state.h
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
//
// Box states for placing the items of one line: the strut of each inline
// box (its text metrics with the half-leading of 'line-height' and emphasis
// mark outsets), 'line-height: normal' used fonts, and 'vertical-align'.
// Inline boxes do not create box fragments here, and there is no SVG text,
// ruby, quirks mode or text-fit scaling.
#pragma once

#include "base/heap_vector.h"
#include "base/vector.h"
#include "font/font_baseline.h"
#include "font/font_height.h"
#include "layout/inline/fragment_item.h"
#include "style/computed_style.h"

namespace bkit {

class ShapeResultView;

// Line items being placed: the item and its block offset from the baseline of
// the line.
using LogicalLineItems = HeapVector<FragmentItem>;

struct InlineBoxState {
  struct PendingPositions {
    wtf_size_t fragment_start;
    wtf_size_t fragment_end;
    FontHeight metrics;
    EVerticalAlign vertical_align;
    wtf_size_t depth;
  };

  wtf_size_t fragment_start = 0;
  const ComputedStyle* style = nullptr;
  const InlineObject* object = nullptr;
  const Font* font = nullptr;

  // The united metrics for the aligned subtree, including itself.
  FontHeight metrics = FontHeight::Empty();
  // The metrics of the font for this box. This includes leadings as specified
  // by the 'line-height' property.
  FontHeight text_metrics = FontHeight::Empty();
  // The distance between the text-top and the baseline for this box. The
  // text-top does not include leadings.
  LayoutUnit text_top;
  // The height of the text fragments.
  LayoutUnit text_height;

  // These box states are pending, waiting for the metrics of this box to be
  // determined.
  Vector<PendingPositions> pending_descendants;
  bool include_used_fonts = false;
  // A LayoutTextCombine box: laid out horizontally, never shifted.
  bool is_text_combine = false;

  // Compute text metrics for a box. All text in a box share the same
  // metrics.
  void ComputeTextMetrics(const ComputedStyle&, const Font&, FontBaseline ifc_baseline);
  void EnsureTextMetrics(const ComputedStyle&, const Font&, FontBaseline ifc_baseline);
  void ResetTextMetrics();
  void AccumulateUsedFonts(const ShapeResultView*);
  LayoutUnit TextTop(FontBaseline) const;
  bool HasMetrics() const { return !metrics.IsEmpty(); }
};

// InlineLayoutStateStack for one line.
class InlineLayoutStateStack {
public:
  // `continuing_boxes` are the inline boxes open at the start of the line,
  // outermost first; they get their struts as upstream's box states of the
  // previous lines do.
  InlineBoxState* OnBeginPlaceItems(const InlineObject& root, std::span<const InlineObject* const> continuing_boxes,
                                    FontBaseline, const LogicalLineItems&);
  // Push a box for an inline box (with its strut) or for an atomic inline
  // (without).
  InlineBoxState* OnOpenTag(const InlineObject&, bool compute_text_metrics, FontBaseline, const LogicalLineItems&);
  void CapturePaintOffsets(FragmentItem&, FontBaseline) const;
  InlineBoxState* OnCloseTag(LogicalLineItems*, InlineBoxState*, FontBaseline);
  void OnEndPlaceItems(LogicalLineItems*, FontBaseline);

  InlineBoxState& LineBoxState() { return stack_.front(); }

private:
  enum PositionPending { kPositionNotPending, kPositionPending };
  void EndBoxState(InlineBoxState*, LogicalLineItems*, FontBaseline);
  PositionPending ApplyBaselineShift(InlineBoxState*, LogicalLineItems*, FontBaseline);
  FontHeight MetricsForTopAndBottomAlign(const InlineBoxState&) const;
  static void MoveInBlockDirection(LogicalLineItems*, LayoutUnit, wtf_size_t start, wtf_size_t end, wtf_size_t depth);

  HeapVector<InlineBoxState, 4> stack_;
};

} // namespace bkit
