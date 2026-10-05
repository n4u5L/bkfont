// Ported from: blink/renderer/core/paint/text_fragment_painter.cc
// Ported from: blink/renderer/core/paint/box_fragment_painter.cc
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "text_fragment_painter.h"

#include <optional>

#include "font/font.h"
#include "font/simple_font_data.h"
#include "font/text_fragment_paint_info.h"
#include "layout/text_combine.h"
#include "paint/line_relative_rect.h"
#include "platform_paint.h"
#include "shaping/shape_result_view.h"
#include "affine_transform.h"

namespace bkfont {

PhysicalRect PhysicalBoxRect(const PhysicalRect& rect_in_container,
                             const PhysicalOffset& paint_offset,
                             const PhysicalOffset& parent_offset,
                             const TextCombine* text_combine) {
  PhysicalRect box_rect = rect_in_container;
  box_rect.offset.left += paint_offset.left;
  // We round the y-axis to ensure consistent line heights.
  box_rect.offset.top =
      LayoutUnit((paint_offset.top + parent_offset.top).Round()) + (box_rect.offset.top - parent_offset.top);
  if (text_combine) {
    box_rect.offset.left = text_combine->AdjustTextLeftForPaint(box_rect.offset.left);
  }
  return box_rect;
}

void PaintTextFragment(PaintCanvas* canvas,
                       const TextFragmentPaintInfo& fragment_paint_info,
                       const Font& scaled_font,
                       WritingMode writing_mode,
                       const PhysicalRect& physical_box,
                       const PlatformPaint& paint,
                       const TextCombine* text_combine,
                       NodeId node_id) {
  // Determine whether or not we’ll need a writing-mode rotation, but don’t
  // actually rotate until we reach the steps that need it.
  std::optional<AffineTransform> rotation;
  const bool is_horizontal = IsHorizontalWritingMode(writing_mode);
  const LineRelativeRect rotated_box = LineRelativeRect::CreateFromLineBox(physical_box, is_horizontal);
  if (!is_horizontal) {
    rotation.emplace(rotated_box.ComputeRelativeToPhysicalTransform(writing_mode));
  }

  // Set our font.
  const Font* font;
  if (text_combine && text_combine->CompressedFont()) [[unlikely]] {
    font = text_combine->CompressedFont();
  } else {
    font = &scaled_font;
  }
  const SimpleFontData* font_data = font->PrimaryFont();

  PaintCanvasAutoRestore state_saver(canvas, /*save=*/false);
  const int ascent = font_data ? font_data->GetFontMetrics().Ascent() : 0;
  LayoutUnit top = physical_box.offset.top + ascent;
  LineRelativeOffset text_origin{physical_box.offset.left, top};
  if (text_combine) [[unlikely]] {
    text_origin.line_over = text_combine->AdjustTextTopForPaint(physical_box.offset.top);
  }

  if (rotation) {
    canvas->Save();
    canvas->Concat(rotation->ToScalarMatrix());
  }

  font->DrawText(canvas, fragment_paint_info, PointF(text_origin), node_id, paint);
}

void PaintTextCombine(PaintCanvas* canvas,
                      const TextCombine& text_combine,
                      const PhysicalOffset& paint_offset,
                      const PlatformPaint& paint,
                      NodeId node_id) {
  PaintCanvasAutoRestore state_saver(canvas, /*save=*/false);
  if (text_combine.NeedsAffineTransformInPaint()) {
    canvas->Save();
    canvas->Concat(text_combine.ComputeAffineTransformForPaint(paint_offset).ToScalarMatrix());
  }

  // The combined text is in one line box at the origin of the box, laid out
  // in 'writing-mode: horizontal-tb'.
  const PhysicalOffset line_box_offset;
  for (const TextCombine::TextItem& item : text_combine.Items()) {
    if (!item.shape_result || item.start == item.end) {
      continue;
    }
    std::unique_ptr<ShapeResultView> view = ShapeResultView::Create(item.shape_result.get());
    const TextFragmentPaintInfo fragment_paint_info{StringView(text_combine.GetTextContent()), item.start, item.end,
                                                    view.get()};
    const PhysicalRect physical_box = PhysicalBoxRect(item.rect, paint_offset, line_box_offset, &text_combine);
    PaintTextFragment(canvas, fragment_paint_info, text_combine.StyleFont(), WritingMode::kHorizontalTb,
                      physical_box, paint, &text_combine, node_id);
  }
}

} // namespace bkfont
