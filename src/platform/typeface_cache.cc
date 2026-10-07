// Ported from: skia/src/core/SkTypefaceCache.cpp

#include "typeface_cache.h"

#include <atomic>
#include <utility>

namespace bkit {

TypefaceCache::TypefaceCache() = default;

void TypefaceCache::Add(std::shared_ptr<Typeface> face) {
  const auto limit = kTypefaceCacheCountLimit;

  if (static_cast<int>(typefaces_.size()) >= limit) {
    Purge(limit >> 2);
  }
  if (limit > 0) {
    typefaces_.push_back(std::move(face));
  }
}

std::shared_ptr<Typeface> TypefaceCache::FindByProcAndRef(FindProc proc, void* ctx) const {
  for (const std::shared_ptr<Typeface>& typeface : typefaces_) {
    if (proc(typeface.get(), ctx)) {
      return typeface;
    }
  }
  return nullptr;
}

void TypefaceCache::Purge(int num_to_purge) {
  int count = static_cast<int>(typefaces_.size());
  int i = 0;
  while (i < count) {
    if (typefaces_[i].use_count() == 1) {
      // removeShuffle.
      typefaces_[i] = std::move(typefaces_.back());
      typefaces_.pop_back();
      --count;
      if (--num_to_purge == 0) {
        return;
      }
    } else {
      ++i;
    }
  }
}

void TypefaceCache::PurgeAll() {
  Purge(static_cast<int>(typefaces_.size()));
}

std::uint32_t TypefaceCache::NewTypefaceID() {
  static std::atomic<std::int32_t> next_id{1};
  return static_cast<std::uint32_t>(next_id.fetch_add(1, std::memory_order_relaxed));
}

} // namespace bkit
