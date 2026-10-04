// Ported from: skia/src/core/SkStrikeSpec.cpp

#include "strike_spec.h"

#include <optional>

#include "matrix.h"
#include "strike.h"
#include "strike_cache.h"

namespace bkfont {

StrikeSpec::StrikeSpec(const PlatformFont& font)
    : typeface_(font.GetTypeface()) {
  // SkStrikeSpec(font, paint, SkSurfaceProps(), kFakeGammaAndBoostContrast,
  // SkMatrix::I()). The effects are always empty.
  ScalerContext::MakeRecAndEffects(font, &descriptor_);
}

StrikeSpec::~StrikeSpec() = default;

std::tuple<StrikeSpec, float> StrikeSpec::MakeCanonicalized(const PlatformFont& font) {
  const PlatformFont* canonicalized_font = &font;
  std::optional<PlatformFont> path_font;
  float strike_to_source_scale = 1;
  if (ShouldDrawAsPath(font)) {
    canonicalized_font = &path_font.emplace(font);
    strike_to_source_scale = path_font->SetupForAsPaths();
  }

  return {StrikeSpec(*canonicalized_font), strike_to_source_scale};
}

bool StrikeSpec::ShouldDrawAsPath(const PlatformFont& font) {
  // hairline glyphs are fast enough, so we don't need to cache them. The
  // default paint is kFill_Style, and SkMatrix::I() has no perspective.

  ScalarMatrix text_matrix = ScalarMatrix::Scale(font.GetSize() * font.GetScaleX(), font.GetSize());
  if (font.GetSkewX()) {
    text_matrix.PostSkew(font.GetSkewX(), 0);
  }
  // postConcat(SkMatrix::I()) is a no-op.

  // we have a self-imposed maximum, just to limit memory-usage
  constexpr float kMemoryLimit = 256;
  constexpr float kMaxSizeSquared = kMemoryLimit * kMemoryLimit;

  auto distance = [](float x, float y) {
    return x * x + y * y;
  };

  return distance(text_matrix.GetScaleX(), text_matrix.GetSkewY()) > kMaxSizeSquared || distance(text_matrix.GetSkewX(), text_matrix.GetScaleY()) > kMaxSizeSquared;
}

std::unique_ptr<ScalerContext> StrikeSpec::CreateScalerContext() const {
  return bkfont::CreateScalerContext(typeface_, descriptor_);
}

std::shared_ptr<Strike> StrikeSpec::FindOrCreateStrike() const {
  return StrikeCache::GlobalStrikeCache()->FindOrCreateStrike(*this);
}

BulkGlyphMetrics::BulkGlyphMetrics(const StrikeSpec& spec)
    : strike_{spec.FindOrCreateStrike()} {
}

BulkGlyphMetrics::~BulkGlyphMetrics() = default;

std::span<const PlatformGlyph*> BulkGlyphMetrics::Glyphs(std::span<const std::uint16_t> glyph_ids) {
  glyphs_.resize(static_cast<wtf_size_t>(glyph_ids.size()));
  return strike_->Metrics(glyph_ids, glyphs_.data());
}

} // namespace bkfont
