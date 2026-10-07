// Ported from: skia/include/core/SkPaint.h

#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>

#include "blend_mode.h"
#include "color4f.h"
#include "mask_gamma.h"
#include "stroke.h"

namespace bkit {

class ImageFilter;
class PathEffect;
class Shader;

// SkPaint, reduced to what text and decoration painting use: color, shader,
// blend mode, anti-aliasing, the fill or stroke style with its stroke
// parameters, a path effect and an image filter. There is no
// kStrokeAndFill_Style, color filter or mask filter.
class PlatformPaint {
public:
  // SkPaint::Style without kStrokeAndFill_Style.
  enum class Style : std::uint8_t {
    kFill,
    kStroke,
  };

  PlatformPaint() = default;
  explicit PlatformPaint(ColorARGB color)
      : color_(Color4f::FromColor(color)) {
  }
  explicit PlatformPaint(const Color4f& color) {
    SetColor(color);
  }

  // SkPaint::getColor, the color rounded to 8 bits per channel.
  ColorARGB GetColor() const {
    return color_.ToColor();
  }
  void SetColor(ColorARGB color) {
    color_ = Color4f::FromColor(color);
  }

  const Color4f& GetColor4f() const {
    return color_;
  }
  // SkPaint::setColor(SkColor4f) preserves extended RGB and pins only alpha.
  void SetColor(const Color4f& color) {
    // SkTPin returns the lower bound for NaN, unlike std::clamp.
    const float alpha = std::max(0.0f, std::min(color.a, 1.0f));
    color_ = {color.r, color.g, color.b, alpha};
  }

  float GetAlphaf() const {
    return color_.a;
  }

  bool IsAntiAlias() const {
    return anti_alias_;
  }
  void SetAntiAlias(bool anti_alias) {
    anti_alias_ = anti_alias;
  }

  BlendMode GetBlendMode() const {
    return blend_mode_;
  }
  void SetBlendMode(BlendMode mode) {
    blend_mode_ = mode;
  }

  const std::shared_ptr<const Shader>& GetShader() const {
    return shader_;
  }
  void SetShader(std::shared_ptr<const Shader> shader) {
    shader_ = std::move(shader);
  }

  Style GetStyle() const {
    return style_;
  }
  void SetStyle(Style style) {
    style_ = style;
  }
  // SkPaint::setStrokeWidth ignores negative widths. Zero is a hairline.
  float GetStrokeWidth() const {
    return stroke_width_;
  }
  void SetStrokeWidth(float width) {
    if (width >= 0) stroke_width_ = width;
  }
  // SkPaint::setStrokeMiter ignores negative limits.
  float GetStrokeMiter() const {
    return stroke_miter_;
  }
  void SetStrokeMiter(float limit) {
    if (limit >= 0) stroke_miter_ = limit;
  }
  StrokeCap GetStrokeCap() const {
    return stroke_cap_;
  }
  void SetStrokeCap(StrokeCap cap) {
    stroke_cap_ = cap;
  }
  StrokeJoin GetStrokeJoin() const {
    return stroke_join_;
  }
  void SetStrokeJoin(StrokeJoin join) {
    stroke_join_ = join;
  }

  const std::shared_ptr<const PathEffect>& GetPathEffect() const {
    return path_effect_;
  }
  void SetPathEffect(std::shared_ptr<const PathEffect> path_effect) {
    path_effect_ = std::move(path_effect);
  }

  // The image filter applied by SaveLayer.
  const std::shared_ptr<const ImageFilter>& GetImageFilter() const {
    return image_filter_;
  }
  void SetImageFilter(std::shared_ptr<const ImageFilter> image_filter) {
    image_filter_ = std::move(image_filter);
  }

  // SkPaint::nothingToDraw is in the canvas; this is whether the paint has
  // effects that change geometry (SkPaint::canComputeFastBounds reversed).
  bool IsFillStyleWithoutEffects() const {
    return style_ == Style::kFill && !path_effect_;
  }

  // operator==(const SkPaint&, const SkPaint&): effect identity and field
  // equality. SkPaint compares cached SkBlender::Mode() singletons, which is
  // equivalent to comparing the blend mode.
  friend bool operator==(const PlatformPaint& a, const PlatformPaint& b) {
    return a.shader_ == b.shader_ && a.path_effect_ == b.path_effect_ && a.image_filter_ == b.image_filter_ &&
           a.color_ == b.color_ && a.stroke_width_ == b.stroke_width_ && a.stroke_miter_ == b.stroke_miter_ &&
           a.blend_mode_ == b.blend_mode_ && a.anti_alias_ == b.anti_alias_ && a.style_ == b.style_ &&
           a.stroke_cap_ == b.stroke_cap_ && a.stroke_join_ == b.stroke_join_;
  }

private:
  // SK_ColorBLACK.
  Color4f color_ = kBlackColor4f;
  std::shared_ptr<const Shader> shader_;
  std::shared_ptr<const PathEffect> path_effect_;
  std::shared_ptr<const ImageFilter> image_filter_;
  float stroke_width_ = 0;
  float stroke_miter_ = kDefaultMiterLimit;
  BlendMode blend_mode_ = BlendMode::kSrcOver;
  bool anti_alias_ = false;
  Style style_ = Style::kFill;
  StrokeCap stroke_cap_ = StrokeCap::kButt;
  StrokeJoin stroke_join_ = StrokeJoin::kMiter;
};

} // namespace bkit
