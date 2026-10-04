// Ported from: skia/src/core/SkStrike.cpp

#include "strike.h"

#include <utility>

#include "strike_cache.h"

namespace bkfont {

Strike::Strike(StrikeCache* strike_cache, const StrikeSpec& strike_spec, std::unique_ptr<ScalerContext> scaler)
    : strike_spec_{strike_spec},
      strike_cache_{strike_cache},
      scaler_context_{std::move(scaler)} {
}

class Strike::Monitor {
public:
  Monitor(Strike* strike)
      : strike_{strike} {
    strike_->Lock();
  }

  ~Monitor() {
    strike_->Unlock();
  }

private:
  Strike* const strike_;
};

void Strike::Lock() {
  strike_lock_.Acquire();
  memory_increase_ = 0;
}

void Strike::Unlock() {
  const std::size_t memory_increase = memory_increase_;
  strike_lock_.Release();
  UpdateMemoryUsage(memory_increase);
}

std::span<const PlatformGlyph*> Strike::Metrics(std::span<const std::uint16_t> glyph_ids, const PlatformGlyph* results[]) {
  Monitor m{this};
  return InternalPrepare(glyph_ids, results);
}

// glyph(SkPackedGlyphID) through digestFor(kDirectMask, ...). The kDirectMask
// action only checks the atlas size and allocates nothing.
PlatformGlyph* Strike::GlyphFor(std::uint16_t glyph_id) {
  auto found = glyph_for_id_.find(glyph_id);
  if (found != glyph_for_id_.end()) {
    return &found->second;
  }

  auto inserted = glyph_for_id_.emplace(glyph_id, scaler_context_->MakeGlyph(glyph_id)).first;
  memory_increase_ += sizeof(PlatformGlyph);
  return &inserted->second;
}

std::span<const PlatformGlyph*> Strike::InternalPrepare(std::span<const std::uint16_t> glyph_ids, const PlatformGlyph** results) {
  // PathDetail::kMetricsOnly.
  const PlatformGlyph** cursor = results;
  for (std::uint16_t glyph_id : glyph_ids) {
    PlatformGlyph* glyph = GlyphFor(glyph_id);
    *cursor++ = glyph;
  }

  return {results, glyph_ids.size()};
}

void Strike::UpdateMemoryUsage(std::size_t increase) {
  if (increase > 0) {
    // removed_ and the cache's total memory are managed under the cache's
    // lock. This allows them to be accessed under LRU operation.
    AutoMutexExclusive lock{strike_cache_->lock_};
    memory_used_ += increase;
    if (!removed_) {
      strike_cache_->total_memory_used_ += increase;
    }
  }
}

} // namespace bkfont
