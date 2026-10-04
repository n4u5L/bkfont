// Port source: third_party/blink/renderer/platform/fonts/font_platform_data_cache.h
/*
 * Copyright (C) 2006, 2008 Apple Computer, Inc.  All rights reserved.
 * Copyright (C) 2007-2008 Torch Mobile, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "font_cache_key.h"

#include <memory>
#include "base/hash_map.h"
namespace bkfont {

enum class AlternateFontName;
class FontCache;
class FontDescription;
class FontFaceCreationParams;
class FontPlatformData;

// `FontPlatformDataCache` is the shared cache mapping from `FontDescription`
// to `FontPlatformData`.
class FontPlatformDataCache final {

public:
  FontPlatformDataCache();

  std::shared_ptr<const FontPlatformData> GetOrCreateFontPlatformData(
      FontCache* font_cache,
      const FontDescription& font_description,
      const FontFaceCreationParams& creation_params,
      AlternateFontName alternate_font_name);

  void Clear() {
    map_.clear();
  }

private:
  HashMap<FontCacheKey, std::weak_ptr<const FontPlatformData>> map_;

  // A maximum float value to which we limit incoming font sizes. This is the
  // smallest float so that multiplying it by
  // FontCacheKey::PrecisionMultiplier() is still smaller than
  // std::numeric_limits<unsigned>::max() - 1 in order to avoid hitting
  // HashMap sentinel values (placed at std::numeric_limits<unsigned>::max()
  // and std::numeric_limits<unsigned>::max() - 1) for FontPlatformDataCache.
  const float font_size_limit_;
};

} // namespace bkfont
