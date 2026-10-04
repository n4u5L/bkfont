// Ported from: skia/include/core/SkFont.h, skia/include/core/SkFontTypes.h

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <utility>

#include "font_face.h"
#include "rect.h"

namespace bkfont {

// SkFontHinting.
enum class FontHinting : std::uint8_t {
  kNone,
  kSlight,
  kNormal,
  kFull
};

// SkFont. A null typeface stands in for SkTypeface::MakeEmpty().
class PlatformFont {
public:
  enum class Edging : std::uint8_t {
    kAlias,
    kAntiAlias,
    kSubpixelAntiAlias
  };

  PlatformFont();
  explicit PlatformFont(std::shared_ptr<FontFace> typeface);
  PlatformFont(std::shared_ptr<FontFace> typeface, float size);
  PlatformFont(std::shared_ptr<FontFace> typeface, float size, float scale_x, float skew_x);

  bool IsForceAutoHinting() const {
    return (flags_ & kForceAutoHintingPrivFlag) != 0;
  }
  bool IsEmbeddedBitmaps() const {
    return (flags_ & kEmbeddedBitmapsPrivFlag) != 0;
  }
  bool IsSubpixel() const {
    return (flags_ & kSubpixelPrivFlag) != 0;
  }
  bool IsLinearMetrics() const {
    return (flags_ & kLinearMetricsPrivFlag) != 0;
  }
  bool IsEmbolden() const {
    return (flags_ & kEmboldenPrivFlag) != 0;
  }
  bool IsBaselineSnap() const {
    return (flags_ & kBaselineSnapPrivFlag) != 0;
  }

  void SetForceAutoHinting(bool force_auto_hinting);
  void SetEmbeddedBitmaps(bool embedded_bitmaps);
  void SetSubpixel(bool subpixel);
  void SetLinearMetrics(bool linear_metrics);
  void SetEmbolden(bool embolden);
  void SetBaselineSnap(bool baseline_snap);

  Edging GetEdging() const {
    return edging_;
  }
  void SetEdging(Edging edging) {
    edging_ = edging;
  }
  FontHinting GetHinting() const {
    return hinting_;
  }
  void SetHinting(FontHinting hinting) {
    hinting_ = hinting;
  }

  const std::shared_ptr<FontFace>& GetTypeface() const {
    return typeface_;
  }
  float GetSize() const {
    return size_;
  }
  float GetScaleX() const {
    return scale_x_;
  }
  float GetSkewX() const {
    return skew_x_;
  }

  void SetTypeface(std::shared_ptr<FontFace> typeface) {
    typeface_ = std::move(typeface);
  }
  void SetSize(float text_size);
  void SetScaleX(float scale_x) {
    scale_x_ = scale_x;
  }
  void SetSkewX(float skew_x) {
    skew_x_ = skew_x;
  }

  float GetWidth(std::uint16_t glyph) const {
    float width;
    GetWidthsBounds({&glyph, 1}, {&width, 1}, {});
    return width;
  }
  void GetWidths(std::span<const std::uint16_t> glyphs, std::span<float> widths) const {
    GetWidthsBounds(glyphs, widths, {});
  }
  void GetWidthsBounds(std::span<const std::uint16_t> glyphs, std::span<float> widths, std::span<ScalarRect> bounds) const;

  // Sets up the font for paths: canonical size, no hinting, subpixel. Returns
  // the strike-to-source scale. Only the null-paint form is used here.
  float SetupForAsPaths();

private:
  enum PrivFlags : std::uint8_t {
    kForceAutoHintingPrivFlag = 1 << 0,
    kEmbeddedBitmapsPrivFlag = 1 << 1,
    kSubpixelPrivFlag = 1 << 2,
    kLinearMetricsPrivFlag = 1 << 3,
    kEmboldenPrivFlag = 1 << 4,
    kBaselineSnapPrivFlag = 1 << 5,
  };

  std::shared_ptr<FontFace> typeface_;
  float size_;
  float scale_x_;
  float skew_x_;
  std::uint8_t flags_;
  Edging edging_;
  FontHinting hinting_;
};

} // namespace bkfont
