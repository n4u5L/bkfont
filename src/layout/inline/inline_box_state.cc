// Adapted from: blink/renderer/core/layout/inline/inline_box_state.cc
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "inline_box_state.h"

#include <cassert>

#include "base/notreached.h"
#include "font/simple_font_data.h"
#include "geometry/length_functions.h"
#include "layout/inline_text_metrics.h"
#include "shaping/shape_result_view.h"

namespace bkit {
namespace {

FontHeight ComputeEmphasisMarkOutsets(const ComputedStyle& style, const Font& font) {
  if (style.GetTextEmphasisMark() == TextEmphasisMark::kNone) return FontHeight::Empty();

  LayoutUnit emphasis_mark_height = LayoutUnit(font.EmphasisMarkHeight(style.TextEmphasisMarkString()));
  assert(emphasis_mark_height >= LayoutUnit());
  return style.GetTextEmphasisLineLogicalSide() == LineLogicalSide::kOver
             ? FontHeight(emphasis_mark_height, LayoutUnit())
             : FontHeight(LayoutUnit(), emphasis_mark_height);
}

} // namespace

void InlineBoxState::ComputeTextMetrics(const ComputedStyle& styleref, const Font& fontref, FontBaseline ifc_baseline) {
  // 'dominant-baseline' is always 'auto' for non-SVG elements.
  const FontBaseline baseline_type = ifc_baseline;
  if (const SimpleFontData* font_data = fontref.PrimaryFont())
    text_metrics = font_data->GetFontMetrics().GetFontHeight(baseline_type);
  else
    text_metrics = FontHeight();
  text_top = -text_metrics.ascent;
  text_height = text_metrics.LineHeight();

  FontHeight emphasis_marks_outsets = ComputeEmphasisMarkOutsets(styleref, fontref);
  LayoutUnit line_height = ComputedLineHeightAsFixed(styleref.LineHeight(), fontref);
  FontHeight leading_space = CalculateLeadingSpace(line_height, text_metrics);
  if (emphasis_marks_outsets.IsEmpty()) {
    text_metrics.AddLeading(leading_space);
  } else {
    FontHeight emphasis_marks_metrics = text_metrics;
    emphasis_marks_metrics += emphasis_marks_outsets;
    text_metrics.AddLeading(leading_space);
    text_metrics.Unite(emphasis_marks_metrics);
  }

  metrics.Unite(text_metrics);

  include_used_fonts = styleref.LineHeight().IsAuto();
}

void InlineBoxState::ResetTextMetrics() {
  metrics = text_metrics = FontHeight::Empty();
  text_top = text_height = LayoutUnit();
}

void InlineBoxState::EnsureTextMetrics(const ComputedStyle& styleref, const Font& fontref, FontBaseline ifc_baseline) {
  if (text_metrics.IsEmpty()) ComputeTextMetrics(styleref, fontref, ifc_baseline);
}

void InlineBoxState::AccumulateUsedFonts(const ShapeResultView* shape_result) {
  const auto baseline_type = style->GetFontBaseline();
  for (const auto& used_font : shape_result->UsedFonts()) {
    FontHeight used_metrics = used_font->GetFontMetrics().GetFontHeight(baseline_type);
    FontHeight leading_space = CalculateLeadingSpace(used_font->GetFontMetrics().FixedLineSpacing(), used_metrics);
    used_metrics.AddLeading(leading_space);
    metrics.Unite(used_metrics);
  }
}

LayoutUnit InlineBoxState::TextTop(FontBaseline baseline_type) const {
  if (!text_metrics.IsEmpty()) return text_top;
  if (const SimpleFontData* font_data = font->PrimaryFont())
    return -font_data->GetFontMetrics().FixedAscent(baseline_type);
  NOTREACHED();
}

InlineBoxState* InlineLayoutStateStack::OnBeginPlaceItems(const InlineObject& root,
                                                          std::span<const InlineObject* const> continuing_boxes,
                                                          FontBaseline baseline_type, const LogicalLineItems& line_box) {
  const ComputedStyle& line_style = *root.LayoutStyle();
  stack_.clear();
  stack_.resize(1);
  InlineBoxState& line_box_state = stack_.back();
  line_box_state.fragment_start = line_box.size();
  line_box_state.style = &line_style;
  line_box_state.object = &root;
  line_box_state.font = line_style.GetFont();
  // Use a "strut" (a zero-width inline box with the element's font and line
  // height properties) as the initial metrics for the line box.
  // https://drafts.csswg.org/css2/visudet.html#strut
  line_box_state.ComputeTextMetrics(line_style, *line_box_state.font, baseline_type);

  // Boxes wrapped from the previous line keep their text metrics as their
  // initial metrics.
  for (const InlineObject* object : continuing_boxes) {
    InlineBoxState* box = OnOpenTag(*object, true, baseline_type, line_box);
    box->metrics = box->text_metrics;
  }
  return &stack_.back();
}

InlineBoxState* InlineLayoutStateStack::OnOpenTag(const InlineObject& object, bool compute_text_metrics,
                                                  FontBaseline baseline_type, const LogicalLineItems& line_box) {
  const ComputedStyle& style = *object.LayoutStyle();
  stack_.resize(stack_.size() + 1);
  InlineBoxState* box = &stack_.back();
  box->fragment_start = line_box.size();
  box->style = &style;
  box->object = &object;
  box->font = style.GetFont();
  // Compute text metrics for all inline boxes since even empty inlines
  // influence the line height.
  // https://drafts.csswg.org/css2/visudet.html#line-height
  if (compute_text_metrics) box->ComputeTextMetrics(style, *box->font, baseline_type);
  return box;
}

void InlineLayoutStateStack::CapturePaintOffsets(FragmentItem& item, FontBaseline baseline) const {
  item.paint_boxes_.ReserveInitialCapacity(stack_.size());
  for (const InlineBoxState& box : stack_) {
    item.paint_boxes_.push_back(FragmentItem::InlinePaintBox{box.object, box.TextTop(baseline)});
  }
}

InlineBoxState* InlineLayoutStateStack::OnCloseTag(LogicalLineItems* line_box, InlineBoxState* box,
                                                   FontBaseline baseline_type) {
  assert(box == &stack_.back());
  EndBoxState(box, line_box, baseline_type);
  stack_.pop_back();
  return &stack_.back();
}

void InlineLayoutStateStack::OnEndPlaceItems(LogicalLineItems* line_box, FontBaseline baseline_type) {
  for (wtf_size_t i = stack_.size(); i-- > 0;) EndBoxState(&stack_[i], line_box, baseline_type);
}

void InlineLayoutStateStack::EndBoxState(InlineBoxState* box, LogicalLineItems* line_box,
                                         FontBaseline baseline_type) {
  PositionPending position_pending = ApplyBaselineShift(box, line_box, baseline_type);

  // We are done here if there is no parent box.
  if (box == stack_.data()) return;
  InlineBoxState& parent_box = *(box - 1);

  // Unite the metrics to the parent box.
  if (position_pending == kPositionNotPending) parent_box.metrics.Unite(box->metrics);
}

void InlineLayoutStateStack::MoveInBlockDirection(LogicalLineItems* line_box, LayoutUnit delta, wtf_size_t start,
                                                  wtf_size_t end, wtf_size_t depth) {
  for (wtf_size_t i = start; i < end; ++i) {
    FragmentItem& item = (*line_box)[i];
    item.block_offset_ += delta;
    for (wtf_size_t j = depth; j < item.paint_boxes_.size(); ++j) item.paint_boxes_[j].block_offset += delta;
  }
}

InlineLayoutStateStack::PositionPending InlineLayoutStateStack::ApplyBaselineShift(InlineBoxState* box,
                                                                                   LogicalLineItems* line_box,
                                                                                   FontBaseline baseline_type) {
  // Some 'vertical-align' values require the size of their parents. Align all
  // such descendant boxes that require the size of this box; they are queued in
  // |pending_descendants|.
  LayoutUnit baseline_shift;
  if (!box->pending_descendants.empty()) {
    bool has_top_or_bottom = false;
    for (InlineBoxState::PendingPositions& child : box->pending_descendants) {
      if (child.metrics.IsEmpty()) child.metrics = FontHeight();
      switch (child.vertical_align) {
        case EVerticalAlign::kTextTop:
          baseline_shift = child.metrics.ascent + box->TextTop(baseline_type);
          break;
        case EVerticalAlign::kTextBottom:
          if (const SimpleFontData* font_data = box->font->PrimaryFont()) {
            LayoutUnit text_bottom = font_data->GetFontMetrics().FixedDescent(baseline_type);
            baseline_shift = text_bottom - child.metrics.descent;
          }
          break;
        case EVerticalAlign::kTop:
        case EVerticalAlign::kBottom: has_top_or_bottom = true; continue;
        default: NOTREACHED();
      }
      child.metrics.Move(baseline_shift);
      box->metrics.Unite(child.metrics);
      MoveInBlockDirection(line_box, baseline_shift, child.fragment_start, child.fragment_end, child.depth);
    }
    // `top` and `bottom` need to be applied after all other values are applied,
    // because they align to the maximum metrics, but the maximum metrics may
    // depend on other pending descendants for this box.
    if (has_top_or_bottom) {
      FontHeight max = MetricsForTopAndBottomAlign(*box);
      for (InlineBoxState::PendingPositions& child : box->pending_descendants) {
        switch (child.vertical_align) {
          case EVerticalAlign::kTop: baseline_shift = child.metrics.ascent - max.ascent; break;
          case EVerticalAlign::kBottom: baseline_shift = max.descent - child.metrics.descent; break;
          case EVerticalAlign::kTextTop:
          case EVerticalAlign::kTextBottom: continue;
          default: NOTREACHED();
        }
        child.metrics.Move(baseline_shift);
        box->metrics.Unite(child.metrics);
        MoveInBlockDirection(line_box, baseline_shift, child.fragment_start, child.fragment_end, child.depth);
      }
    }
    box->pending_descendants.clear();
  }

  const ComputedStyle& style = *box->style;
  EVerticalAlign vertical_align = style.VerticalAlign();
  if (vertical_align == EVerticalAlign::kBaseline) return kPositionNotPending;

  // Text content in text-combine-upright:all is layout in horizontally, so
  // we don't need to move text combine box.
  if (box->is_text_combine) [[unlikely]] {
    return kPositionNotPending;
  }

  // Check if there are any fragments to move.
  wtf_size_t fragment_end = line_box->size();
  if (box->fragment_start == fragment_end) return kPositionNotPending;

  // 'vertical-align' aligns boxes relative to themselves, to their parent
  // boxes, or to the line box, depends on the value.
  // Because |box| is an item in |stack_|, |box[-1]| is its parent box.
  // If this box doesn't have a parent; i.e., this box is a line box,
  // 'vertical-align' has no effect.
  if (box == stack_.data()) return kPositionNotPending;
  InlineBoxState& parent_box = *(box - 1);

  switch (vertical_align) {
    case EVerticalAlign::kSub: baseline_shift = parent_box.style->ComputedFontSizeAsFixed() / 5 + 1; break;
    case EVerticalAlign::kSuper: baseline_shift = -(parent_box.style->ComputedFontSizeAsFixed() / 3 + 1); break;
    case EVerticalAlign::kLength: {
      // 'Percentages: refer to the 'line-height' of the element itself'.
      // https://www.w3.org/TR/CSS22/visudet.html#propdef-vertical-align
      const Length& length = style.GetVerticalAlignLength();
      LayoutUnit line_height =
          length.HasPercent() ? style.ComputedLineHeightAsFixed() : box->text_metrics.LineHeight();
      baseline_shift = -ValueForLength(length, line_height);
      break;
    }
    case EVerticalAlign::kMiddle:
      baseline_shift = (box->metrics.ascent - box->metrics.descent) / 2;
      if (const SimpleFontData* parent_font_data = parent_box.style->GetFont()->PrimaryFont())
        baseline_shift -= LayoutUnit::FromFloatRound(parent_font_data->GetFontMetrics().XHeight() / 2);
      break;
    case EVerticalAlign::kBaselineMiddle: baseline_shift = (box->metrics.ascent - box->metrics.descent) / 2; break;
    case EVerticalAlign::kTop:
    case EVerticalAlign::kBottom: {
      // 'top' and 'bottom' require the layout size of the nearest ancestor that
      // has 'top' or 'bottom', or the line box if none.
      InlineBoxState* ancestor = &parent_box;
      for (; ancestor != stack_.data(); --ancestor) {
        if (ancestor->style->VerticalAlign() == EVerticalAlign::kTop ||
            ancestor->style->VerticalAlign() == EVerticalAlign::kBottom)
          break;
      }
      ancestor->pending_descendants.push_back(
          InlineBoxState::PendingPositions{box->fragment_start, fragment_end, box->metrics, vertical_align,
                                            static_cast<wtf_size_t>(box - stack_.data())});
      return kPositionPending;
    }
    default:
      // Other values require the layout size of the parent box.
      parent_box.pending_descendants.push_back(
          InlineBoxState::PendingPositions{box->fragment_start, fragment_end, box->metrics, vertical_align,
                                            static_cast<wtf_size_t>(box - stack_.data())});
      return kPositionPending;
  }
  if (!box->metrics.IsEmpty()) box->metrics.Move(baseline_shift);
  MoveInBlockDirection(line_box, baseline_shift, box->fragment_start, fragment_end,
                         static_cast<wtf_size_t>(box - stack_.data()));
  return kPositionNotPending;
}

FontHeight InlineLayoutStateStack::MetricsForTopAndBottomAlign(const InlineBoxState& box) const {
  assert(!box.pending_descendants.empty());

  // |metrics| is the bounds of "aligned subtree", that is, bounds of
  // descendants that are not 'vertical-align: top' nor 'bottom'.
  // https://drafts.csswg.org/css2/visudet.html#propdef-vertical-align
  // Inline boxes create no box fragments here, so there is no box data to
  // include.
  FontHeight metrics = box.metrics;
  if (metrics.IsEmpty()) metrics = FontHeight();

  // If the height of a box that has 'vertical-align: top' or 'bottom' exceeds
  // the height of the "aligned subtree", align the edge to the "aligned
  // subtree" and extend the other edge.
  FontHeight max = metrics;
  for (const InlineBoxState::PendingPositions& child : box.pending_descendants) {
    if ((child.vertical_align == EVerticalAlign::kTop || child.vertical_align == EVerticalAlign::kBottom) &&
        child.metrics.LineHeight() > max.LineHeight()) {
      if (child.vertical_align == EVerticalAlign::kTop)
        max = FontHeight(metrics.ascent, child.metrics.LineHeight() - metrics.ascent);
      else if (child.vertical_align == EVerticalAlign::kBottom)
        max = FontHeight(child.metrics.LineHeight() - metrics.descent, metrics.descent);
    }
  }
  return max;
}

} // namespace bkit
