// Ported from: blink/renderer/core/paint/text_decoration_painter.cc
// Ported from: blink/renderer/core/paint/text_painter.cc (ClipDecorationLine)
#include "text_decoration_painter.h"

#include "font/font.h"
#include "font/text_fragment_paint_info.h"
#include "text_decoration_info.h"
#include "text_paint_style.h"

namespace bkit {
namespace {
void ClipDecorationLine(PaintCanvas* canvas, const DecorationGeometry& geometry, float baseline,
                        const PointF& origin, const Font& font, const TextFragmentPaintInfo& text,
                        const PlatformPaint& paint) {
  if (text.from >= text.to || !text.shape_result) return;
  ScalarRect bounds = DecorationLinePainter::Bounds(geometry);
  bounds.Inset(0, 0.5f);
  const float upper = bounds.top - baseline;
  const float height = bounds.Height();
  Vector<Font::TextIntercept> intercepts;
  font.GetTextIntercepts(text, paint, std::make_tuple(upper, upper + height), intercepts);
  const float dilation = std::min(geometry.Thickness(), 13.0f);
  for (const auto& intercept : intercepts) {
    ScalarRect clip = ScalarRect::MakeXYWH(origin.x() + intercept.begin_, origin.y() + upper,
                                          intercept.end_ - intercept.begin_, height);
    clip.Outset(dilation, 1);
    if (clip.IsFinite()) canvas->ClipOutRect(clip, false);
  }
}
} // namespace

void PaintTextDecorations(PaintCanvas* canvas, const TextFragmentPaintInfo& text, const Font& font,
                           const PointF& origin, const LineRelativeRect& frame, const ComputedStyle& style,
                           const TextPaintStyle& text_style, bool line_through, bool shadow_phase,
                           std::span<const DecoratingBox> decorating_boxes) {
  size_t index = 0;
  for (const auto& decoration : style.AppliedTextDecorations()) {
    TextDecorationInfo info({frame.LineLeft().ToFloat(), frame.LineOver().ToFloat()}, frame.InlineSize().ToFloat(),
                             style, font, decoration, index < decorating_boxes.size() ? &decorating_boxes[index] : nullptr);
    ++index;
    const Color4f color = shadow_phase ? kBlackColor4f : info.Color();
    const auto paint = [&](const DecorationGeometry& geometry, bool skip_ink) {
      PaintCanvasAutoRestore restore(canvas, true);
      if (skip_ink && style.TextDecorationSkipInk() == ETextDecorationSkipInk::kAuto) {
        ClipDecorationLine(canvas, geometry, info.Baseline(), origin, font, text, text_style.FillPaint());
      }
      DecorationLinePainter::Paint(canvas, geometry, color);
    };
    if (line_through) {
      if (info.HasLineThrough()) paint(info.LineThrough(), false);
    } else if (info.HasError()) {
      paint(info.ErrorLine(), false);
    } else if (font.PrimaryFont()) {
      if (info.HasUnderline()) paint(info.Underline(), true);
      if (info.HasOverline()) paint(info.Overline(), true);
    }
  }
}
} // namespace bkit
