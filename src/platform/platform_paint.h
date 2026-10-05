// Ported from: skia/include/core/SkPaint.h

#pragma once

#include <algorithm>
#include <memory>
#include <utility>

#include "blend_mode.h"
#include "color4f.h"
#include "mask_gamma.h"

namespace bkfont {

class Shader;

// SkPaint, reduced to what glyph drawing uses: color, shader, blend mode and
// anti-aliasing. The style is always kFill_Style and there is no color filter,
// path effect, mask filter or image filter, so the stroke fields of the
// scaler context rec keep their fill values.
class PlatformPaint {
public:
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

private:
  // SK_ColorBLACK.
  Color4f color_ = kBlackColor4f;
  std::shared_ptr<const Shader> shader_;
  BlendMode blend_mode_ = BlendMode::kSrcOver;
  bool anti_alias_ = false;
};

} // namespace bkfont
