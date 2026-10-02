/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// Port source: platform/fonts/font_data_cache.h; weak references replace tracing.
#pragma once
#include "font_platform_data.h"
#include "simple_font_data.h"
#include "wtf/hash_map.h"
#include "wtf/linked_hash_set.h"
namespace blink {
struct FontDataCacheKeyHashTraits : GenericHashTraits<std::shared_ptr<const FontPlatformData>> {
  static unsigned GetHash(const std::shared_ptr<const FontPlatformData>& data) {
    return data->GetHash();
  }
  static bool Equal(const std::shared_ptr<const FontPlatformData>& a, const std::shared_ptr<const FontPlatformData>& b) {
    return *a == *b;
  }
  static constexpr bool kSafeToCompareToEmptyOrDeleted = false;
};
class FontDataCache final {
public:
  FontDataCache() = default;
  FontDataCache(const FontDataCache&) = delete;
  FontDataCache& operator=(const FontDataCache&) = delete;
  std::shared_ptr<const SimpleFontData> Get(std::shared_ptr<const FontPlatformData>, bool subpixel_ascent_descent = false);
  void Clear() {
    cache_.clear();
    strong_reference_lru_.clear();
  }

private:
  HashMap<std::shared_ptr<const FontPlatformData>, std::weak_ptr<const SimpleFontData>, FontDataCacheKeyHashTraits> cache_;
  LinkedHashSet<std::shared_ptr<const SimpleFontData>> strong_reference_lru_;
};
} // namespace blink
