// Ported from: skia/src/core/SkTypefaceCache.h

#pragma once

#include <cstdint>
#include <memory>

#include "base/vector.h"
#include "typeface.h"

namespace bkit {

// SkGraphics::GetTypefaceCacheCountLimit's historical default value.
inline constexpr int kTypefaceCacheCountLimit = 1024;

// SkTypefaceCache.
class TypefaceCache {
public:
  TypefaceCache();
  TypefaceCache(const TypefaceCache&) = delete;
  TypefaceCache& operator=(const TypefaceCache&) = delete;

  // Callback for FindByProc. Returns true if the given typeface is a match
  // for the given context. The passed typeface is owned by the cache and is
  // not additionally ref()ed. The typeface may be in the disposed state.
  using FindProc = bool (*)(Typeface*, void* context);

  // Add a typeface to the cache. Later, if we need to purge the cache,
  // typefaces uniquely owned by the cache will be unref()ed.
  void Add(std::shared_ptr<Typeface> face);

  // Iterate through the cache, calling proc(typeface, ctx) for each typeface.
  // If proc returns true, then return that typeface. If it never returns
  // true, return null.
  std::shared_ptr<Typeface> FindByProcAndRef(FindProc proc, void* ctx) const;

  // This will unref all of the typefaces in the cache for which the cache is
  // the only owner. Normally this is handled automatically as needed. This
  // function is exposed for clients that explicitly want to purge the cache
  // (e.g. to look for leaks).
  void PurgeAll();

  // Helper: returns a unique typefaceID to pass to the constructor of your
  // subclass of Typeface.
  static std::uint32_t NewTypefaceID();

private:
  void Purge(int count);

  Vector<std::shared_ptr<Typeface>> typefaces_;
};

} // namespace bkit
