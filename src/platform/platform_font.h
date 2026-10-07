// Ported from: skia/include/core/SkFont.h
// Ported from: skia/include/core/SkFontTypes.h

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <utility>

#include "platform_font_metrics.h"
#include "paint/rect.h"
#include "typeface.h"

namespace bkit {

// SkFontHinting.
enum class FontHinting : std::uint8_t {
  kNone,
  kSlight,
  kNormal,
  kFull
};

// SkFont. The typeface is never null; a null argument becomes
// Typeface::MakeEmpty() as upstream.
class PlatformPaint;

class PlatformFont {
public:
  enum class Edging : std::uint8_t {
    kAlias,
    kAntiAlias,
    kSubpixelAntiAlias
  };

  PlatformFont();
  explicit PlatformFont(std::shared_ptr<Typeface> typeface);
  PlatformFont(std::shared_ptr<Typeface> typeface, float size);
  PlatformFont(std::shared_ptr<Typeface> typeface, float size, float scale_x, float skew_x);

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
  bool HasSomeAntiAliasing() const {
    Edging edging = GetEdging();
    return edging == Edging::kAntiAlias || edging == Edging::kSubpixelAntiAlias;
  }
  FontHinting GetHinting() const {
    return hinting_;
  }
  void SetHinting(FontHinting hinting) {
    hinting_ = hinting;
  }

  const std::shared_ptr<Typeface>& GetTypeface() const {
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

  bool operator==(const PlatformFont& other) const;

  void SetTypeface(std::shared_ptr<Typeface> typeface);
  void SetSize(float text_size);
  void SetScaleX(float scale_x) {
    scale_x_ = scale_x;
  }
  void SetSkewX(float skew_x) {
    skew_x_ = skew_x;
  }

  // Returns glyph index for Unicode character. If the character is not
  // supported by the Typeface, returns 0.
  std::uint16_t UnicharToGlyph(std::int32_t uni) const {
    return typeface_->UnicharToGlyph(uni);
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

  // Retrieves the bounds for each glyph in glyphs. Only the null-paint form
  // is used here.
  void GetBounds(std::span<const std::uint16_t> glyphs, std::span<ScalarRect> bounds) const {
    GetWidthsBounds(glyphs, {}, bounds);
  }
  ScalarRect GetBounds(std::uint16_t glyph) const {
    ScalarRect bounds;
    GetBounds({&glyph, 1}, {&bounds, 1});
    return bounds;
  }

  // Returns the advance width of the glyphs, and their bounds relative to
  // the origin when bounds is not null. Only the glyph encoding is used.
  float MeasureText(std::span<const std::uint16_t> glyphs, ScalarRect* bounds, const PlatformPaint* paint = nullptr) const;

  // Returns PlatformFontMetrics associated with Typeface. The return value is
  // the recommended spacing between lines: the sum of metrics descent,
  // ascent, and leading.
  float GetMetrics(PlatformFontMetrics* metrics) const;

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

  std::shared_ptr<Typeface> typeface_;
  float size_;
  float scale_x_;
  float skew_x_;
  std::uint8_t flags_;
  Edging edging_;
  FontHinting hinting_;
};

} // namespace bkit
