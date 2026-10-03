// Port source: third_party/blink/renderer/platform/fonts/font_fallback_map.h
// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "font_cache_client.h"
#include "font_description.h"
#include "font_fallback_list.h"
#include "font_selector_client.h"
#include "base/vector.h"

#include <memory>
#include <span>
#include "base/vector.h"
#include "base/hash_map.h"
namespace blink {

class FontSelector;

// This class acts as a cache ensuring that equivalent `FontDescription`s will
// have the same `FontFallbackList`.
//
// This class doesn't retain the `FontFallbackList`s however, only having a weak
// reference to them.
class FontFallbackMap : public FontCacheClient,
                        public FontSelectorClient {
public:
  explicit FontFallbackMap(std::shared_ptr<FontSelector> font_selector)
      : font_selector_(font_selector) {
  }

  std::shared_ptr<FontSelector> GetFontSelector() const {
    return font_selector_.lock();
  }

  std::shared_ptr<FontFallbackList> Get(const FontDescription& font_description);

private:
  // FontSelectorClient
  void FontsNeedUpdate(FontSelector*, FontInvalidationReason) override;

  // FontCacheClient
  void FontCacheInvalidated() override;

  void InvalidateAll();

  template <typename Predicate>
  void InvalidateInternal(Predicate predicate);

  const std::weak_ptr<FontSelector> font_selector_;
  HashMap<FontDescription, std::weak_ptr<FontFallbackList>>
      fallback_list_for_description_;
};

} // namespace blink
