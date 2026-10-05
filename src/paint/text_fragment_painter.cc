// Ported from: blink/renderer/core/paint/text_fragment_painter.cc
// Ported from: blink/renderer/core/paint/box_fragment_painter.cc
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "text_fragment_painter.h"

#include <optional>

#include "font/font.h"
#include "font/simple_font_data.h"
#include "font/plain_text_node.h"
#include "font/text_fragment_paint_info.h"
#include "layout/text_combine.h"
#include "paint/line_relative_rect.h"
#include "platform_paint.h"
#include "shaping/shape_result_view.h"
#include "affine_transform.h"
#include "style/computed_style.h"
#include "text/text_run.h"
#include "text_decoration_painter.h"
#include "text_painter.h"
#include "text_shadow_painter.h"

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
                       NodeId node_id,
                       const ComputedStyle* text_style, std::span<const DecoratingBox> decorating_boxes) {
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

  if (text_style) {
    TextPainter::Paint(canvas, fragment_paint_info, *font, PointF(text_origin), rotated_box, *text_style, node_id, decorating_boxes);
  } else {
    font->DrawText(canvas, fragment_paint_info, PointF(text_origin), node_id, paint);
  }
}

void PaintTextFragment(PaintCanvas* canvas, const TextFragmentPaintInfo& info, const Font& font,
                       WritingMode writing_mode, const PhysicalRect& box, const ComputedStyle& style, NodeId node,
                       std::span<const DecoratingBox> decorating_boxes) {
  PaintTextFragment(canvas, info, font, writing_mode, box, style.TextPaint(), nullptr, node, &style, decorating_boxes);
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

void PaintTextCombine(PaintCanvas* canvas, const TextCombine& combine, const PhysicalOffset& offset,
                      const ComputedStyle& style, NodeId node_id) {
  const TextPaintStyle paint_style = TextPaintStyle::FromStyle(style);
  const LineRelativeRect frame = combine.ComputeTextFrameRect(offset);
  const Font& parent_font = *style.GetFont();
  const SimpleFontData* data = parent_font.PrimaryFont();
  const PointF origin(frame.LineLeft().ToFloat(), frame.LineOver().ToFloat() + (data ? data->GetFontMetrics().Ascent() : 0));

  // The combined composition takes one emphasis mark. Blink uses U+3042 as
  // a representative ideograph because U+FFFC has unsuitable glyph metrics.
  const UChar placeholder_character = 0x3042;
  const String placeholder(std::span<const UChar>(&placeholder_character, 1));
  std::unique_ptr<PlainTextNode> emphasis_node;
  if (style.GetTextEmphasisMark() != TextEmphasisMark::kNone) {
    const TextRun run{StringView(placeholder)};
    emphasis_node = std::make_unique<PlainTextNode>(run, false, parent_font, false, nullptr);
  }
  // Match the ordinary fragment order, including each stage's shadows.
  const auto lines = style.TextDecorationsInEffect();
  const auto before_text = TextDecorationLine::kUnderline | TextDecorationLine::kOverline |
                           TextDecorationLine::kSpellingError | TextDecorationLine::kGrammarError;
  for (int phase = 0; phase < 3; ++phase) {
  if (phase == 0 && (lines & before_text) == TextDecorationLine::kNone) continue;
  if (phase == 2 && (lines & TextDecorationLine::kLineThrough) == TextDecorationLine::kNone) continue;
  PaintWithTextShadow(canvas, paint_style, [&](PaintCanvas* canvas, bool shadow) {
    const auto decorations = [&](bool through) {
      PaintCanvasAutoRestore restore(canvas, true);
      canvas->Concat(frame.ComputeRelativeToPhysicalTransform(style.GetWritingMode()).ToScalarMatrix());
      PaintTextDecorations(canvas, TextFragmentPaintInfo{}, parent_font, origin, frame, style, paint_style, through, shadow);
    };
    if (phase == 0) { decorations(false); return; }
    if (phase == 2) { decorations(true); return; }
    {
      PaintCanvasAutoRestore restore(canvas, false);
      if (combine.NeedsAffineTransformInPaint()) {
        canvas->Save();
        canvas->Concat(combine.ComputeAffineTransformForPaint(offset).ToScalarMatrix());
      }
      for (const auto& item : combine.Items()) {
        if (!item.shape_result || item.start == item.end) continue;
        const auto view = ShapeResultView::Create(item.shape_result.get());
        const TextFragmentPaintInfo info{StringView(combine.GetTextContent()), item.start, item.end, view.get()};
        const PhysicalRect box = PhysicalBoxRect(item.rect, offset, PhysicalOffset(), &combine);
        PaintTextFragment(canvas, info, combine.StyleFont(), WritingMode::kHorizontalTb, box,
                           paint_style.FillPaint(shadow), &combine, node_id);
        if (paint_style.stroke_width > 0) {
          PaintTextFragment(canvas, info, combine.StyleFont(), WritingMode::kHorizontalTb, box,
                             paint_style.StrokePaint(shadow), &combine, node_id);
        }
      }
    }
    if (emphasis_node && !emphasis_node->ItemList().empty()) {
      if (const auto* view = emphasis_node->ItemList()[0].EnsureView()) {
        PaintCanvasAutoRestore restore(canvas, true);
        canvas->Concat(frame.ComputeRelativeToPhysicalTransform(style.GetWritingMode()).ToScalarMatrix());
        const TextFragmentPaintInfo info{StringView(placeholder), 0, 1, view};
        TextPainter::PaintEmphasis(canvas, info, parent_font, origin, style, paint_style, shadow);
      }
    }
  });
  }
}

} // namespace bkfont
