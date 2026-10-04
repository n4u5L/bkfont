// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license.
// Ported from platform/fonts/shaping/harfbuzz_font_cache.cc.
#include "harfbuzz_font_cache.h"
namespace bkfont {
HarfBuzzFontCache& HarfBuzzFontCache::Get() {
  static thread_local HarfBuzzFontCache cache;
  return cache;
}
} // namespace bkfont
