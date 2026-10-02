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

// Port source: platform/fonts/font_data_cache.cc.
#include "font_data_cache.h"
namespace blink {
std::shared_ptr<const SimpleFontData> FontDataCache::Get(std::shared_ptr<const FontPlatformData> platform_data, bool subpixel_ascent_descent) {
  if (!platform_data || !platform_data->GetFontFace()) return nullptr;
  // Upstream GC removes entries whose weak font data is dead. Without GC,
  // discard those entries on lookup, retaining the original 64-entry LRU policy.
  Vector<std::shared_ptr<const FontPlatformData>> expired;
  for (const auto& entry : cache_)
    if (entry.value.expired()) expired.push_back(entry.key);
  for (const auto& key : expired) cache_.erase(key);
  auto added = cache_.insert(platform_data, std::weak_ptr<const SimpleFontData>());
  auto result = added.stored_value->value.lock();
  if (!result) {
    result = std::make_shared<SimpleFontData>(platform_data, nullptr, subpixel_ascent_descent);
    added.stored_value->value = result;
  }
  strong_reference_lru_.PrependOrMoveToFirst(result);
  while (strong_reference_lru_.size() > 64) strong_reference_lru_.pop_back();
  return result;
}
} // namespace blink
