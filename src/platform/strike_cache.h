// Ported from: skia/src/core/SkStrikeCache.h

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>

#include "base/mutex.h"
#include "scaler_context.h"
#include "strike_spec.h"

namespace bkit {

class Strike;

// SK_DEFAULT_FONT_CACHE_COUNT_LIMIT, which Chromium only overrides on
// Windows. The FreeType rasterizer keeps no GDI handles, so the count limit
// of the FreeType platforms applies.
inline constexpr int kDefaultFontCacheCountLimit = 2048;
// content::InitializeSkia calls SkGraphics::SetFontCacheLimit(kMB) in every
// renderer, which replaces the SK_DEFAULT_FONT_CACHE_LIMIT define.
inline constexpr std::size_t kDefaultFontCacheLimit = 1024 * 1024;

// SkStrikeCache. Pinners are not ported; every strike is deletable.
class StrikeCache final {
public:
  StrikeCache() = default;
  StrikeCache(const StrikeCache&) = delete;
  StrikeCache& operator=(const StrikeCache&) = delete;

  static StrikeCache* GlobalStrikeCache();

  std::shared_ptr<Strike> FindOrCreateStrike(const StrikeSpec& strike_spec);

  int GetCacheCountLimit() const;
  int SetCacheCountLimit(int limit);
  int GetCacheCountUsed() const;

  std::size_t GetCacheSizeLimit() const;
  std::size_t SetCacheSizeLimit(std::size_t limit);
  std::size_t GetTotalMemoryUsed() const;

private:
  friend class Strike; // for Strike::UpdateMemoryUsage

  std::shared_ptr<Strike> InternalFindStrikeOrNull(const ScalerContextRec& desc);
  std::shared_ptr<Strike> InternalCreateStrike(const StrikeSpec& strike_spec);

  // The following methods can only be called when lock_ is held.
  void InternalRemoveStrike(Strike* strike);
  void InternalAttachToHead(std::shared_ptr<Strike> strike);

  // Checkout budgets, modulated by the specified min-bytes-needed-to-purge,
  // and attempt to purge caches to match.
  // Returns number of bytes freed.
  std::size_t InternalPurge(std::size_t min_bytes_needed = 0);

  mutable Mutex lock_;
  Strike* head_{nullptr};
  Strike* tail_{nullptr};
  std::unordered_map<ScalerContextRec, std::shared_ptr<Strike>, ScalerContextRecHash> strike_lookup_;

  std::size_t cache_size_limit_{kDefaultFontCacheLimit};
  std::size_t total_memory_used_{0};
  std::int32_t cache_count_limit_{kDefaultFontCacheCountLimit};
  std::int32_t cache_count_{0};
};

} // namespace bkit
