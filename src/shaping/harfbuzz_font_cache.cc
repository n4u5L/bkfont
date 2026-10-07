// Ported from: blink/renderer/platform/fonts/shaping/harfbuzz_font_cache.cc

// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license.
#include "harfbuzz_font_cache.h"
namespace bkit {

HarfBuzzFontCache& HarfBuzzFontCache::Get() {
  static thread_local HarfBuzzFontCache cache;
  return cache;
}

} // namespace bkit
