// Ported from: blink/renderer/core/paint/text_paint_style.h
#pragma once

#include <memory>
#include "platform_paint.h"
#include "style/shadow_list.h"

namespace bkit {
class ComputedStyle;
struct TextPaintStyle {
  Color4f current_color;
  Color4f fill_color;
  Color4f stroke_color;
  Color4f emphasis_mark_color;
  float stroke_width = 0;
  std::shared_ptr<const ShadowList> shadow;
  PlatformPaint base_paint;

  static TextPaintStyle FromStyle(const ComputedStyle&);
  PlatformPaint FillPaint(bool shadow_phase = false) const;
  PlatformPaint StrokePaint(bool shadow_phase = false) const;
};
} // namespace bkit
