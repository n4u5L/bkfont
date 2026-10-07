// Ported from: blink/renderer/core/paint/text_decoration_painter.h
#pragma once
#include <span>
#include "line_relative_rect.h"
#include "paint_canvas.h"

namespace bkit {
class ComputedStyle;
class Font;
struct TextFragmentPaintInfo;
struct TextPaintStyle;
struct DecoratingBox;
void PaintTextDecorations(PaintCanvas*, const TextFragmentPaintInfo&, const Font&, const PointF& text_origin,
                           const LineRelativeRect&, const ComputedStyle&, const TextPaintStyle&,
                           bool line_through, bool shadow_phase, std::span<const DecoratingBox> = {});
} // namespace bkit
