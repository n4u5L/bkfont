// Ported from: skia/src/core/SkStrikeSpec.h

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <tuple>

#include "base/vector.h"
#include "font_face.h"
#include "platform_font.h"
#include "platform_glyph.h"
#include "scaler_context.h"

namespace bkfont {

class Strike;

class StrikeSpec {
public:
  StrikeSpec(const StrikeSpec&) = default;
  StrikeSpec& operator=(const StrikeSpec&) = delete;

  StrikeSpec(StrikeSpec&&) = default;
  StrikeSpec& operator=(StrikeSpec&&) = delete;

  ~StrikeSpec();

  // Create a strike spec for mask style cache entries.
  // MakeCanonicalized(font, nullptr): the null paint is the only form used.
  static std::tuple<StrikeSpec, float> MakeCanonicalized(const PlatformFont& font);

  std::unique_ptr<ScalerContext> CreateScalerContext() const;

  const ScalerContextRec& Descriptor() const {
    return descriptor_;
  }

  std::shared_ptr<Strike> FindOrCreateStrike() const;

  // ShouldDrawAsPath(SkPaint(), font, SkMatrix::I()).
  static bool ShouldDrawAsPath(const PlatformFont& font);

private:
  explicit StrikeSpec(const PlatformFont& font);

  ScalerContextRec descriptor_;
  std::shared_ptr<FontFace> typeface_;
};

class BulkGlyphMetrics {
public:
  explicit BulkGlyphMetrics(const StrikeSpec& spec);
  ~BulkGlyphMetrics();
  std::span<const PlatformGlyph*> Glyphs(std::span<const std::uint16_t> glyph_ids);

private:
  inline static constexpr wtf_size_t kTypicalGlyphCount = 20;
  Vector<const PlatformGlyph*, kTypicalGlyphCount> glyphs_;
  std::shared_ptr<Strike> strike_;
};

} // namespace bkfont
