// Ported from Chromium: third_party/blink/renderer/platform/fonts/shaping/ng_shape_cache.h
/*
 * Copyright (C) 2012 Apple Inc. All rights reserved.
 * Copyright (C) 2015 Google Inc. All rights reserved.
 * Copyright (C) 2023 Igalia S.L. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <memory>
#include "base/hash/hash.h"
#include "base/hash_map.h"
#include "shape_result.h"

#include "runtime_enabled_features.h"
#include "text/text_direction.h"
#include "base/hash_functions.h"
#include "base/hash_table_deleted_value_type.h"
#include "base/text/wtf_string.h"

namespace blink {

class NGShapeCache {
public:
  static constexpr unsigned kMaxTextLengthOfEntries = 30;
  static constexpr unsigned kMaxSize = 2048;

  explicit NGShapeCache(const SimpleFontData* primary_font)
      : primary_font_(primary_font) {
  }
  NGShapeCache(const NGShapeCache&) = delete;
  NGShapeCache& operator=(const NGShapeCache&) = delete;

  template <typename ShapeResultFunc>
  std::shared_ptr<const ShapeResult> GetOrCreate(const String& text,
                                                 TextDirection direction,
                                                 const ShapeResultFunc& shape_result_func) {
    if (text.length() > kMaxTextLengthOfEntries) {
      return shape_result_func();
    }

    auto& map = IsLtr(direction) ? ltr_string_map_ : rtl_string_map_;
    // The source GC clears weak map entries. With no C++ GC, expire entries
    // here before applying the original 2048-entry capacity branch.
    map.erase_if([](const auto& entry) { return entry.value.expired(); });
    if (map.size() >= kMaxSize) [[unlikely]] {
      const auto it = map.find(text);
      if (it != map.end()) {
        if (auto cached = it->value.lock()) return cached;
      }
      return shape_result_func();
    }

    const auto add_result = map.insert(text, std::weak_ptr<const ShapeResult>{});
    if (auto cached = add_result.stored_value->value.lock()) {
      return cached;
    }

    auto result = shape_result_func();

    // Only shape-results without font-fallback are valid, because the cache is
    // in the `primary_font_`.
    if (!result->HasFallbackFonts(primary_font_)) {
      add_result.stored_value->value = result;
    }

    return result;
  }

private:
  typedef HashMap<String, std::weak_ptr<const ShapeResult>> SmallStringMap;

  SmallStringMap ltr_string_map_;
  SmallStringMap rtl_string_map_;
  const SimpleFontData* primary_font_;
};

} // namespace blink
