// Ported from: blink/renderer/core/paint/text_painter.cc
// Ported from: blink/renderer/platform/graphics/graphics_context.cc (DrawTextPasses)
#include "text_painter.h"

#include "font/font.h"
#include "font/simple_font_data.h"
#include "font/text_fragment_paint_info.h"
#include "style/computed_style.h"
#include "text_decoration_painter.h"
#include "text_shadow_painter.h"

namespace bkfont {
void TextPainter::PaintText(PaintCanvas* canvas, const TextFragmentPaintInfo& text, const Font& font, PointF origin,
                            NodeId node, const TextPaintStyle& style, bool shadow) {
  font.DrawText(canvas, text, origin, node, style.FillPaint(shadow));
  if (style.stroke_width > 0) font.DrawText(canvas, text, origin, node, style.StrokePaint(shadow));
}
void TextPainter::PaintEmphasis(PaintCanvas* canvas, const TextFragmentPaintInfo& text, const Font& font, PointF origin,
                                const ComputedStyle& style, const TextPaintStyle& paint_style, bool shadow) {
  if (style.GetTextEmphasisMark() == TextEmphasisMark::kNone) return;
  const SimpleFontData* data = font.PrimaryFont();
  if (!data) return;
  const auto& mark = style.TextEmphasisMarkString();
  if (mark.IsNull()) return;
  const float offset = style.GetTextEmphasisLineLogicalSide() == LineLogicalSide::kOver ?
      -data->GetFontMetrics().Ascent() - font.EmphasisMarkDescent(mark) :
      data->GetFontMetrics().Descent() + font.EmphasisMarkAscent(mark);
  origin.set_y(origin.y() + offset);
  PlatformPaint paint = paint_style.FillPaint(shadow);
  paint.SetColor(shadow ? kBlackColor4f : paint_style.emphasis_mark_color);
  font.DrawEmphasisMarks(canvas, text, mark, origin, paint);
  if (paint_style.stroke_width > 0) {
    font.DrawEmphasisMarks(canvas, text, mark, origin, paint_style.StrokePaint(shadow));
  }
}
void TextPainter::Paint(PaintCanvas* canvas, const TextFragmentPaintInfo& text, const Font& font, PointF origin,
                        const LineRelativeRect& frame, const ComputedStyle& style, NodeId node,
                        std::span<const DecoratingBox> decorating_boxes) {
  const TextPaintStyle paint_style = TextPaintStyle::FromStyle(style);
  const auto lines = style.TextDecorationsInEffect();
  const auto before_text = TextDecorationLine::kUnderline | TextDecorationLine::kOverline |
                           TextDecorationLine::kSpellingError | TextDecorationLine::kGrammarError;
  if ((lines & before_text) != TextDecorationLine::kNone) {
    PaintWithTextShadow(canvas, paint_style, [&](PaintCanvas* canvas, bool shadow) {
      PaintTextDecorations(canvas, text, font, origin, frame, style, paint_style, false, shadow, decorating_boxes);
    });
  }
  PaintWithTextShadow(canvas, paint_style, [&](PaintCanvas* canvas, bool shadow) {
    PaintText(canvas, text, font, origin, node, paint_style, shadow);
    PaintEmphasis(canvas, text, font, origin, style, paint_style, shadow);
  });
  if ((lines & TextDecorationLine::kLineThrough) != TextDecorationLine::kNone) {
    PaintWithTextShadow(canvas, paint_style, [&](PaintCanvas* canvas, bool shadow) {
      PaintTextDecorations(canvas, text, font, origin, frame, style, paint_style, true, shadow, decorating_boxes);
    });
  }
}
} // namespace bkfont
