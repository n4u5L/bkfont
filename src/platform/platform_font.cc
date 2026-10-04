// Ported from: skia/src/core/SkFont.cpp

#include "platform_font.h"

#include <algorithm>

#include "strike_spec.h"

namespace bkfont {

namespace {

// SkPaintDefaults_TextSize.
constexpr float kDefaultSize = 12;
// SkFontPriv::kCanonicalTextSizeForPaths.
constexpr int kCanonicalTextSizeForPaths = 64;

float ValidSize(float size) {
  return std::max<float>(0, size);
}

std::uint8_t SetClearMask(std::uint8_t bits, bool cond, std::uint8_t mask) {
  return static_cast<std::uint8_t>(cond ? bits | mask : bits & ~mask);
}

ScalarRect ScalePos(ScalarRect r, float s) {
  return {r.left * s, r.top * s, r.right * s, r.bottom * s};
}

} // namespace

PlatformFont::PlatformFont(std::shared_ptr<FontFace> typeface, float size, float scale_x, float skew_x)
    : typeface_(std::move(typeface)),
      size_(ValidSize(size)),
      scale_x_(scale_x),
      skew_x_(skew_x),
      flags_(kBaselineSnapPrivFlag),
      edging_(Edging::kAntiAlias),
      hinting_(FontHinting::kNormal) {
}

PlatformFont::PlatformFont(std::shared_ptr<FontFace> typeface, float size)
    : PlatformFont(std::move(typeface), size, 1, 0) {
}

PlatformFont::PlatformFont(std::shared_ptr<FontFace> typeface)
    : PlatformFont(std::move(typeface), kDefaultSize, 1, 0) {
}

PlatformFont::PlatformFont()
    : PlatformFont(nullptr, kDefaultSize) {
}

void PlatformFont::SetForceAutoHinting(bool force_auto_hinting) {
  flags_ = SetClearMask(flags_, force_auto_hinting, kForceAutoHintingPrivFlag);
}
void PlatformFont::SetEmbeddedBitmaps(bool embedded_bitmaps) {
  flags_ = SetClearMask(flags_, embedded_bitmaps, kEmbeddedBitmapsPrivFlag);
}
void PlatformFont::SetSubpixel(bool subpixel) {
  flags_ = SetClearMask(flags_, subpixel, kSubpixelPrivFlag);
}
void PlatformFont::SetLinearMetrics(bool linear_metrics) {
  flags_ = SetClearMask(flags_, linear_metrics, kLinearMetricsPrivFlag);
}
void PlatformFont::SetEmbolden(bool embolden) {
  flags_ = SetClearMask(flags_, embolden, kEmboldenPrivFlag);
}
void PlatformFont::SetBaselineSnap(bool baseline_snap) {
  flags_ = SetClearMask(flags_, baseline_snap, kBaselineSnapPrivFlag);
}

void PlatformFont::SetSize(float text_size) {
  size_ = ValidSize(text_size);
}

float PlatformFont::SetupForAsPaths() {
  constexpr std::uint8_t kFlagsToIgnore = kEmbeddedBitmapsPrivFlag | kForceAutoHintingPrivFlag;

  flags_ = static_cast<std::uint8_t>((flags_ & ~kFlagsToIgnore) | kSubpixelPrivFlag);
  SetHinting(FontHinting::kNone);

  if (GetEdging() == Edging::kSubpixelAntiAlias) {
    SetEdging(Edging::kAntiAlias);
  }

  const float text_size = size_;
  SetSize(static_cast<float>(kCanonicalTextSizeForPaths));
  return text_size / kCanonicalTextSizeForPaths;
}

void PlatformFont::GetWidthsBounds(std::span<const std::uint16_t> glyph_ids, std::span<float> widths, std::span<ScalarRect> bounds) const {
  auto [strike_spec, strike_to_source_scale] = StrikeSpec::MakeCanonicalized(*this);
  BulkGlyphMetrics metrics{strike_spec};
  std::span<const PlatformGlyph*> glyphs = metrics.Glyphs(glyph_ids);

  if (bounds.size()) {
    const std::size_t n = std::min(bounds.size(), glyphs.size());
    for (std::size_t i = 0; i < n; ++i) {
      bounds[i] = ScalePos(glyphs[i]->Rect(), strike_to_source_scale);
    }
  }

  if (widths.size()) {
    const std::size_t n = std::min(widths.size(), glyphs.size());
    for (std::size_t i = 0; i < n; ++i) {
      widths[i] = glyphs[i]->AdvanceX() * strike_to_source_scale;
    }
  }
}

} // namespace bkfont
