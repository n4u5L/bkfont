// Ported from: skia/src/core/SkStrike.h

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>

#include "base/mutex.h"
#include "platform_glyph.h"
#include "scaler_context.h"
#include "strike_spec.h"

namespace bkfont {

class StrikeCache;

// SkStrike, metrics only. The font metrics, rounding spec and pinner are not
// ported: nothing on the advance path reads them.
class Strike final : public std::enable_shared_from_this<Strike> {
public:
  Strike(StrikeCache* strike_cache, const StrikeSpec& strike_spec, std::unique_ptr<ScalerContext> scaler);
  Strike(const Strike&) = delete;
  Strike& operator=(const Strike&) = delete;

  void Lock();
  void Unlock();

  std::span<const PlatformGlyph*> Metrics(std::span<const std::uint16_t> glyph_ids, const PlatformGlyph* results[]);

  const ScalerContextRec& GetDescriptor() const {
    return strike_spec_.Descriptor();
  }

private:
  friend class StrikeCache;
  class Monitor;

  PlatformGlyph* GlyphFor(std::uint16_t glyph_id);

  void UpdateMemoryUsage(std::size_t increase);

  // InternalPrepare will only be called with a mutex already held.
  std::span<const PlatformGlyph*> InternalPrepare(std::span<const std::uint16_t> glyph_ids, const PlatformGlyph** results);

  // The following are const and need no mutex protection.
  const StrikeSpec strike_spec_;
  StrikeCache* const strike_cache_;

  // This mutex provides protection for this specific Strike.
  mutable Mutex strike_lock_;

  // Maps from a glyph id to its SkGlyph. Node-based, so a glyph keeps its
  // address as long as the strike is alive, as the arena does upstream.
  std::unordered_map<std::uint16_t, PlatformGlyph> glyph_for_id_;

  // Context that corresponds to the glyph information in this strike.
  const std::unique_ptr<ScalerContext> scaler_context_;

  // Used while changing the strike to track memory increase.
  std::size_t memory_increase_{0};

  // The following are protected by the StrikeCache's mutex.
  Strike* next_{nullptr};
  Strike* prev_{nullptr};
  std::size_t memory_used_{sizeof(Strike)};
  bool removed_{false};
};

} // namespace bkfont
