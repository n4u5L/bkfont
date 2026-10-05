// Ported from: skia/src/core/SkStrike.h

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include "arena.h"
#include "base/mutex.h"
#include "platform_font_metrics.h"
#include "platform_glyph.h"
#include "scaler_context.h"
#include "strike_spec.h"

namespace bkfont {

class StrikeCache;

// SkStrike. The pinner and the remote glyph cache merging are not ported.
// Glyphs, images and paths live in the strike's arena, so they stay valid
// while a reference to the strike is held, even after the cache has purged
// it.
class Strike final : public std::enable_shared_from_this<Strike> {
public:
  Strike(StrikeCache* strike_cache, const StrikeSpec& strike_spec, std::unique_ptr<ScalerContext> scaler);
  Strike(const Strike&) = delete;
  Strike& operator=(const Strike&) = delete;

  void Lock();
  void Unlock();

  // The following require the strike lock to be held.
  GlyphDigest DigestFor(GlyphActionType action_type, PackedGlyphID packed_glyph_id);
  bool PrepareForImage(PlatformGlyph* glyph);
  bool PrepareForPath(PlatformGlyph* glyph);
  bool PrepareForDrawable(PlatformGlyph* glyph);
  PlatformGlyph* Glyph(GlyphDigest digest);

  const PlatformFontMetrics& GetFontMetrics() const {
    return font_metrics_;
  }

  std::span<const PlatformGlyph*> Metrics(std::span<const std::uint16_t> glyph_ids, const PlatformGlyph* results[]);

  std::span<const PlatformGlyph*> PreparePaths(std::span<const std::uint16_t> glyph_ids, const PlatformGlyph* results[]);

  std::span<const PlatformGlyph*> PrepareImages(std::span<const PackedGlyphID> glyph_ids, const PlatformGlyph* results[]);

  std::span<const PlatformGlyph*> PrepareDrawables(std::span<const std::uint16_t> glyph_ids, const PlatformGlyph* results[]);

  void FindIntercepts(const float bounds[2], float scale, float x_pos,
                      PlatformGlyph* glyph, float* array, int* count);

  const ScalerContextRec& GetDescriptor() const {
    return strike_spec_.Descriptor();
  }

  const GlyphPositionRoundingSpec& RoundingSpec() const {
    return rounding_spec_;
  }

  const StrikeSpec& GetStrikeSpec() const {
    return strike_spec_;
  }

private:
  friend class StrikeCache;
  class Monitor;

  // Return a glyph. Create it if it doesn't exist, and initialize the glyph
  // with metrics and advances using a scaler.
  PlatformGlyph* Glyph(PackedGlyphID packed_glyph_id);

  GlyphDigest* AddGlyphAndDigest(PlatformGlyph* glyph);

  void UpdateMemoryUsage(std::size_t increase);

  enum PathDetail {
    kMetricsOnly,
    kMetricsAndPath
  };

  // InternalPrepare will only be called with a mutex already held.
  std::span<const PlatformGlyph*> InternalPrepare(std::span<const std::uint16_t> glyph_ids, PathDetail path_detail, const PlatformGlyph** results);

  // The following are const and need no mutex protection.
  const PlatformFontMetrics font_metrics_;
  const GlyphPositionRoundingSpec rounding_spec_;
  const StrikeSpec strike_spec_;
  StrikeCache* const strike_cache_;

  // This mutex provides protection for this specific Strike.
  mutable Mutex strike_lock_;

  // Maps from a combined GlyphID and sub-pixel position to a GlyphDigest. The
  // actual glyph is stored in the glyph_for_index_. The GlyphDigest's index_
  // field stores the index. This pointer provides an unchanging reference to
  // the PlatformGlyph as long as the strike is alive, and glyph_for_index_
  // provides a dense index for glyphs.
  std::unordered_map<std::uint32_t, GlyphDigest> digest_for_packed_glyph_id_;

  // Maps from a glyph index to a glyph
  std::vector<PlatformGlyph*> glyph_for_index_;

  // Context that corresponds to the glyph information in this strike.
  const std::unique_ptr<ScalerContext> scaler_context_;

  // Used while changing the strike to track memory increase.
  std::size_t memory_increase_{0};

  // So, we don't grow our arena too fast.
  Arena alloc_;

  // The following are protected by the StrikeCache's mutex.
  Strike* next_{nullptr};
  Strike* prev_{nullptr};
  std::size_t memory_used_{sizeof(Strike)};
  bool removed_{false};
};

} // namespace bkfont
