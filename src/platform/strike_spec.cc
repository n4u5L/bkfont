// Ported from: skia/src/core/SkStrikeSpec.cpp

#include "strike_spec.h"

#include <optional>
#include <utility>

#include "strike.h"
#include "strike_cache.h"

namespace bkfont {

namespace {

// SkFontPriv::MakeTextMatrix.
ScalarMatrix MakeTextMatrix(const PlatformFont& font) {
  ScalarMatrix m = ScalarMatrix::Scale(font.GetSize() * font.GetScaleX(), font.GetSize());
  if (font.GetSkewX()) {
    m.PostSkew(font.GetSkewX(), 0);
  }
  return m;
}

} // namespace

StrikeSpec::StrikeSpec(const PlatformFont& font,
                       const PlatformPaint& paint,
                       const SurfaceProps& surface_props,
                       ScalerContextFlags scaler_context_flags,
                       const ScalarMatrix& device_matrix)
    : typeface_(font.GetTypeface()) {
  // CreateDescriptorAndEffectsUsingPaint.
  ScalerContext::MakeRecAndEffects(font, paint, surface_props, scaler_context_flags, device_matrix, &descriptor_);
}

StrikeSpec::~StrikeSpec() = default;

StrikeSpec StrikeSpec::MakeMask(const PlatformFont& font,
                                const PlatformPaint& paint,
                                const SurfaceProps& surface_props,
                                ScalerContextFlags scaler_context_flags,
                                const ScalarMatrix& device_matrix) {
  return StrikeSpec(font, paint, surface_props, scaler_context_flags, device_matrix);
}

StrikeSpec StrikeSpec::MakeTransformMask(const PlatformFont& font,
                                         const PlatformPaint& paint,
                                         const SurfaceProps& surface_props,
                                         ScalerContextFlags scaler_context_flags,
                                         const ScalarMatrix& device_matrix) {
  PlatformFont source_font{font};
  source_font.SetSubpixel(false);
  return StrikeSpec(source_font, paint, surface_props, scaler_context_flags, device_matrix);
}

std::tuple<StrikeSpec, float> StrikeSpec::MakePath(const PlatformFont& font,
                                                   const PlatformPaint& paint,
                                                   const SurfaceProps& surface_props,
                                                   ScalerContextFlags scaler_context_flags) {
  // setup our std run paint, in hopes of getting hits in the cache
  PlatformPaint path_paint{paint};
  PlatformFont path_font{font};

  // The sub-pixel position will always happen when transforming to the
  // screen.
  path_font.SetSubpixel(false);

  // The factor to get from the size stored in the strike to the size needed
  // for the source. The caller applies the stroke and effect when drawing
  // these canonical outlines, so neither belongs in the path strike.
  float strike_to_source_scale = path_font.SetupForAsPaths();
  path_paint.SetStyle(PlatformPaint::Style::kFill);
  path_paint.SetPathEffect(nullptr);

  return {StrikeSpec(path_font, path_paint, surface_props, scaler_context_flags, ScalarMatrix()),
          strike_to_source_scale};
}

std::tuple<StrikeSpec, float> StrikeSpec::MakeCanonicalized(const PlatformFont& font, const PlatformPaint* paint) {
  PlatformPaint canonicalized_paint;
  if (paint != nullptr) {
    canonicalized_paint = *paint;
  }

  const PlatformFont* canonicalized_font = &font;
  std::optional<PlatformFont> path_font;
  float strike_to_source_scale = 1;
  if (ShouldDrawAsPath(canonicalized_paint, font, ScalarMatrix())) {
    canonicalized_font = &path_font.emplace(font);
    strike_to_source_scale = path_font->SetupForAsPaths();
    canonicalized_paint = PlatformPaint();
  }

  return {StrikeSpec(*canonicalized_font, canonicalized_paint, SurfaceProps(),
                     ScalerContextFlags::kFakeGammaAndBoostContrast, ScalarMatrix()),
          strike_to_source_scale};
}

StrikeSpec StrikeSpec::MakeWithNoDevice(const PlatformFont& font, const PlatformPaint* paint) {
  PlatformPaint setup_paint;
  if (paint != nullptr) {
    setup_paint = *paint;
  }

  return StrikeSpec(font, setup_paint, SurfaceProps(),
                    ScalerContextFlags::kFakeGammaAndBoostContrast, ScalarMatrix());
}

bool StrikeSpec::ShouldDrawAsPath(const PlatformPaint& paint, const PlatformFont& font, const ScalarMatrix& view_matrix) {
  // Hairline glyphs are fast enough that they need not be cached.
  if (paint.GetStyle() == PlatformPaint::Style::kStroke && paint.GetStrokeWidth() == 0) {
    return true;
  }
  // ScalarMatrix has no perspective.

  ScalarMatrix text_matrix = MakeTextMatrix(font);
  text_matrix.PostConcat(view_matrix);

  // we have a self-imposed maximum, just to limit memory-usage
  constexpr float kMemoryLimit = 256;
  constexpr float kMaxSizeSquared = kMemoryLimit * kMemoryLimit;

  auto distance = [](float x, float y) {
    return x * x + y * y;
  };

  return distance(text_matrix.GetScaleX(), text_matrix.GetSkewY()) > kMaxSizeSquared ||
         distance(text_matrix.GetSkewX(), text_matrix.GetScaleY()) > kMaxSizeSquared;
}

std::unique_ptr<ScalerContext> StrikeSpec::CreateScalerContext() const {
  return typeface_->CreateScalerContext(descriptor_);
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

const PlatformGlyph* BulkGlyphMetrics::Glyph(std::uint16_t glyph_id) {
  return Glyphs(std::span<const std::uint16_t>{&glyph_id, 1})[0];
}

BulkGlyphMetricsAndPaths::BulkGlyphMetricsAndPaths(const StrikeSpec& spec)
    : strike_{spec.FindOrCreateStrike()} {
}

BulkGlyphMetricsAndPaths::BulkGlyphMetricsAndPaths(std::shared_ptr<Strike>&& strike)
    : strike_{std::move(strike)} {
}

BulkGlyphMetricsAndPaths::~BulkGlyphMetricsAndPaths() = default;

std::span<const PlatformGlyph*> BulkGlyphMetricsAndPaths::Glyphs(std::span<const std::uint16_t> glyph_ids) {
  glyphs_.resize(static_cast<wtf_size_t>(glyph_ids.size()));
  return strike_->PreparePaths(glyph_ids, glyphs_.data());
}

const PlatformGlyph* BulkGlyphMetricsAndPaths::Glyph(std::uint16_t glyph_id) {
  return Glyphs(std::span<const std::uint16_t>{&glyph_id, 1})[0];
}

void BulkGlyphMetricsAndPaths::FindIntercepts(const float bounds[2], float scale, float x_pos,
                                              const PlatformGlyph* glyph, float* array, int* count) {
  // TODO(herb): remove this abominable const_cast. Do the intercepts really
  // need to be on the glyph?
  strike_->FindIntercepts(bounds, scale, x_pos, const_cast<PlatformGlyph*>(glyph), array, count);
}

BulkGlyphMetricsAndDrawables::BulkGlyphMetricsAndDrawables(const StrikeSpec& spec)
    : strike_{spec.FindOrCreateStrike()} {
}

BulkGlyphMetricsAndDrawables::BulkGlyphMetricsAndDrawables(std::shared_ptr<Strike>&& strike)
    : strike_{std::move(strike)} {
}

BulkGlyphMetricsAndDrawables::~BulkGlyphMetricsAndDrawables() = default;

std::span<const PlatformGlyph*> BulkGlyphMetricsAndDrawables::Glyphs(std::span<const std::uint16_t> glyph_ids) {
  glyphs_.resize(static_cast<wtf_size_t>(glyph_ids.size()));
  return strike_->PrepareDrawables(glyph_ids, glyphs_.data());
}

const PlatformGlyph* BulkGlyphMetricsAndDrawables::Glyph(std::uint16_t glyph_id) {
  return Glyphs(std::span<const std::uint16_t>{&glyph_id, 1})[0];
}

BulkGlyphMetricsAndImages::BulkGlyphMetricsAndImages(const StrikeSpec& spec)
    : strike_{spec.FindOrCreateStrike()} {
}

BulkGlyphMetricsAndImages::BulkGlyphMetricsAndImages(std::shared_ptr<Strike>&& strike)
    : strike_{std::move(strike)} {
}

BulkGlyphMetricsAndImages::~BulkGlyphMetricsAndImages() = default;

std::span<const PlatformGlyph*> BulkGlyphMetricsAndImages::Glyphs(std::span<const PackedGlyphID> packed_ids) {
  glyphs_.resize(static_cast<wtf_size_t>(packed_ids.size()));
  return strike_->PrepareImages(packed_ids, glyphs_.data());
}

const PlatformGlyph* BulkGlyphMetricsAndImages::Glyph(PackedGlyphID packed_id) {
  return Glyphs(std::span<const PackedGlyphID>{&packed_id, 1})[0];
}

const ScalerContextRec& BulkGlyphMetricsAndImages::Descriptor() const {
  return strike_->GetDescriptor();
}

} // namespace bkfont
