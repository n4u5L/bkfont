// Ported from: skia/src/core/SkStrikeSpec.h

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <tuple>

#include "base/vector.h"
#include "matrix.h"
#include "platform_font.h"
#include "platform_glyph.h"
#include "platform_paint.h"
#include "scaler_context.h"
#include "surface_props.h"
#include "typeface.h"

namespace bkfont {

class Strike;

// SkStrikeSpec. The descriptor is the ScalerContextRec: there are never
// effects. The StrikeForGPU cache interface is not ported.
class StrikeSpec {
public:
  StrikeSpec(const StrikeSpec&) = default;
  StrikeSpec& operator=(const StrikeSpec&) = delete;

  StrikeSpec(StrikeSpec&&) = default;
  StrikeSpec& operator=(StrikeSpec&&) = delete;

  ~StrikeSpec();

  // Create a strike spec for mask style cache entries.
  static StrikeSpec MakeMask(const PlatformFont& font,
                             const PlatformPaint& paint,
                             const SurfaceProps& surface_props,
                             ScalerContextFlags scaler_context_flags,
                             const ScalarMatrix& device_matrix);

  // A strike for finding the max size for transforming masks. This is used to
  // calculate the maximum dimension of a SubRun of text.
  static StrikeSpec MakeTransformMask(const PlatformFont& font,
                                      const PlatformPaint& paint,
                                      const SurfaceProps& surface_props,
                                      ScalerContextFlags scaler_context_flags,
                                      const ScalarMatrix& device_matrix);

  // Create a strike spec for path style cache entries.
  static std::tuple<StrikeSpec, float> MakePath(const PlatformFont& font,
                                                const PlatformPaint& paint,
                                                const SurfaceProps& surface_props,
                                                ScalerContextFlags scaler_context_flags);

  // Create a canonical strike spec for device-less measurements.
  static std::tuple<StrikeSpec, float> MakeCanonicalized(const PlatformFont& font, const PlatformPaint* paint = nullptr);

  // Create a strike spec without a device, and does not switch over to path
  // for large sizes.
  static StrikeSpec MakeWithNoDevice(const PlatformFont& font, const PlatformPaint* paint = nullptr);

  std::shared_ptr<Strike> FindOrCreateStrike() const;

  std::unique_ptr<ScalerContext> CreateScalerContext() const;

  const ScalerContextRec& Descriptor() const {
    return descriptor_;
  }

  const Typeface& GetTypeface() const {
    return *typeface_;
  }

  // The paint is always a fill paint and the matrix has no perspective, so
  // only the size of the text matrix decides.
  static bool ShouldDrawAsPath(const PlatformPaint& paint, const PlatformFont& font, const ScalarMatrix& matrix);

private:
  StrikeSpec(const PlatformFont& font,
             const PlatformPaint& paint,
             const SurfaceProps& surface_props,
             ScalerContextFlags scaler_context_flags,
             const ScalarMatrix& device_matrix);

  ScalerContextRec descriptor_;
  std::shared_ptr<Typeface> typeface_;
};

class BulkGlyphMetrics {
public:
  explicit BulkGlyphMetrics(const StrikeSpec& spec);
  ~BulkGlyphMetrics();
  std::span<const PlatformGlyph*> Glyphs(std::span<const std::uint16_t> glyph_ids);
  const PlatformGlyph* Glyph(std::uint16_t glyph_id);

private:
  inline static constexpr wtf_size_t kTypicalGlyphCount = 20;
  Vector<const PlatformGlyph*, kTypicalGlyphCount> glyphs_;
  std::shared_ptr<Strike> strike_;
};

class BulkGlyphMetricsAndPaths {
public:
  explicit BulkGlyphMetricsAndPaths(const StrikeSpec& spec);
  explicit BulkGlyphMetricsAndPaths(std::shared_ptr<Strike>&& strike);
  ~BulkGlyphMetricsAndPaths();
  std::span<const PlatformGlyph*> Glyphs(std::span<const std::uint16_t> glyph_ids);
  const PlatformGlyph* Glyph(std::uint16_t glyph_id);
  void FindIntercepts(const float bounds[2], float scale, float x_pos,
                      const PlatformGlyph* glyph, float* array, int* count);

private:
  inline static constexpr wtf_size_t kTypicalGlyphCount = 20;
  Vector<const PlatformGlyph*, kTypicalGlyphCount> glyphs_;
  std::shared_ptr<Strike> strike_;
};

class BulkGlyphMetricsAndDrawables {
public:
  explicit BulkGlyphMetricsAndDrawables(const StrikeSpec& spec);
  explicit BulkGlyphMetricsAndDrawables(std::shared_ptr<Strike>&& strike);
  ~BulkGlyphMetricsAndDrawables();
  std::span<const PlatformGlyph*> Glyphs(std::span<const std::uint16_t> glyph_ids);
  const PlatformGlyph* Glyph(std::uint16_t glyph_id);

private:
  inline static constexpr wtf_size_t kTypicalGlyphCount = 20;
  Vector<const PlatformGlyph*, kTypicalGlyphCount> glyphs_;
  std::shared_ptr<Strike> strike_;
};

class BulkGlyphMetricsAndImages {
public:
  explicit BulkGlyphMetricsAndImages(const StrikeSpec& spec);
  explicit BulkGlyphMetricsAndImages(std::shared_ptr<Strike>&& strike);
  ~BulkGlyphMetricsAndImages();
  std::span<const PlatformGlyph*> Glyphs(std::span<const PackedGlyphID> packed_ids);
  const PlatformGlyph* Glyph(PackedGlyphID packed_id);
  const ScalerContextRec& Descriptor() const;

private:
  inline static constexpr wtf_size_t kTypicalGlyphCount = 64;
  Vector<const PlatformGlyph*, kTypicalGlyphCount> glyphs_;
  std::shared_ptr<Strike> strike_;
};

} // namespace bkfont
