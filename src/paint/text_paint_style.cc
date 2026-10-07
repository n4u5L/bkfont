// Ported from: blink/renderer/core/paint/text_painter.cc
#include "text_paint_style.h"
#include "style/computed_style.h"

namespace bkit {
TextPaintStyle TextPaintStyle::FromStyle(const ComputedStyle& style) {
  return {style.Color(), style.ResolvedTextFillColor(), style.ResolvedTextStrokeColor(),
          style.ResolvedTextEmphasisColor(), style.TextStrokeWidth(), style.SharedTextShadow(), style.LegacyPaint()};
}
PlatformPaint TextPaintStyle::FillPaint(bool shadow_phase) const {
  PlatformPaint paint = base_paint;
  paint.SetStyle(PlatformPaint::Style::kFill);
  paint.SetColor(shadow_phase ? kBlackColor4f : fill_color);
  return paint;
}
PlatformPaint TextPaintStyle::StrokePaint(bool shadow_phase) const {
  PlatformPaint paint = base_paint;
  paint.SetStyle(PlatformPaint::Style::kStroke);
  paint.SetStrokeWidth(stroke_width);
  paint.SetColor(shadow_phase ? kBlackColor4f : stroke_color);
  return paint;
}
} // namespace bkit
