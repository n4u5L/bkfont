// Ported from: blink/renderer/platform/fonts/shaping/harfbuzz_font_cache.h

// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license.
#pragma once
#include <cstdint>
#include <memory>
#include "base/hash_map.h"
#include "base/hash_traits.h"

namespace bkit {

class FontPlatformData;
struct HarfBuzzFontData;

// FontGlobalContext owns one of these per thread upstream. Entries share a
// HarfBuzz font across FontPlatformData sizes having the same typeface ID.
class HarfBuzzFontCache final {
public:
  static HarfBuzzFontCache& Get();
  std::shared_ptr<HarfBuzzFontData> GetOrCreate(
      uint64_t unique_id, const FontPlatformData* platform_data);

private:
  HashMap<uint64_t, std::weak_ptr<HarfBuzzFontData>,
          IntWithZeroKeyHashTraits<uint64_t>>
      font_map_;
};

} // namespace bkit
