// Ported from: blink/renderer/core/paint/text_painter.h
#pragma once
#include <span>
#include "line_relative_rect.h"
#include "paint_canvas.h"
#include "text_paint_style.h"

namespace bkfont {
class ComputedStyle;
class Font;
struct TextFragmentPaintInfo;
struct DecoratingBox;
class TextPainter {
public:
  // One paint phase; the caller can put the shadow phase into a filtered layer.
  static void PaintText(PaintCanvas*, const TextFragmentPaintInfo&, const Font&, PointF origin,
                         NodeId, const TextPaintStyle&, bool shadow_phase);
  static void PaintEmphasis(PaintCanvas*, const TextFragmentPaintInfo&, const Font&, PointF origin,
                             const ComputedStyle&, const TextPaintStyle&, bool shadow_phase);
  // Underline/overline, text and emphasis, then line-through, with shadows.
  static void Paint(PaintCanvas*, const TextFragmentPaintInfo&, const Font&, PointF origin,
                     const LineRelativeRect&, const ComputedStyle&, NodeId = kInvalidNodeId,
                     std::span<const DecoratingBox> = {});
};
} // namespace bkfont
