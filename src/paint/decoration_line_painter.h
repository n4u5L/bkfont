// Ported from: blink/renderer/core/paint/decoration_line_painter.h
#pragma once

#include "paint_canvas.h"
#include "style/computed_style_base_constants.h"

namespace bkfont {
struct WaveDefinition {
  float wavelength = 0;
  float control_point_distance = 0;
  float phase = 0;
  bool operator==(const WaveDefinition&) const = default;
};
struct DecorationGeometry {
  ETextDecorationStyle style = ETextDecorationStyle::kSolid;
  ScalarRect line;
  float double_offset = 0;
  float wavy_offset = 0;
  WaveDefinition wave;
  bool antialias = false;
  float Thickness() const { return line.Height(); }
  static DecorationGeometry Make(ETextDecorationStyle, ScalarRect, float double_offset, float wavy_offset,
                                  const WaveDefinition* custom_wave = nullptr);
};
class DecorationLinePainter {
public:
  static ScalarRect Bounds(const DecorationGeometry&);
  static void Paint(PaintCanvas*, const DecorationGeometry&, Color4f);
};
} // namespace bkfont
