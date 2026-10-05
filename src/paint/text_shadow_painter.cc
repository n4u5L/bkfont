// Ported from: blink/renderer/core/paint/text_shadow_painter.cc
#include "text_shadow_painter.h"
#include "image_filter.h"

namespace bkfont {
std::shared_ptr<const ImageFilter> MakeTextShadowFilter(const TextPaintStyle& style) {
  if (!style.shadow) return nullptr;
  std::vector<std::shared_ptr<const ImageFilter>> filters;
  const auto& shadows = style.shadow->Shadows();
  for (auto it = shadows.rbegin(); it != shadows.rend(); ++it) {
    const ShadowData& shadow = *it;
    Color4f color = shadow.GetColor().Resolve(style.current_color);
    color.a *= shadow.Opacity();
    if (color.a == 0) continue;
    const float sigma = BlurRadiusToStdDev(shadow.Blur());
    filters.push_back(ImageFilter::DropShadow(shadow.X(), shadow.Y(), sigma, sigma, color));
  }
  return ImageFilter::Merge(filters);
}
} // namespace bkfont
