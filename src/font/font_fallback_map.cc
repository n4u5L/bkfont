// Ported from: blink/renderer/platform/fonts/font_fallback_map.cc
// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "font_fallback_map.h"

#include "font_selector.h"

namespace bkfont {

std::shared_ptr<FontFallbackList> FontFallbackMap::Get(const FontDescription& description) {
  // Replace upstream weak-map GC cleanup without retaining dead descriptions.
  Vector<FontDescription> expired;
  for (const auto& entry : fallback_list_for_description_)
    if (entry.value.expired()) expired.push_back(entry.key);
  for (const auto& key : expired) fallback_list_for_description_.erase(key);
  auto added = fallback_list_for_description_.insert(description, std::weak_ptr<FontFallbackList>());
  auto result = added.stored_value->value.lock();
  if (!result) {
    result = std::make_shared<FontFallbackList>(font_selector_.lock());
    added.stored_value->value = result;
  }
  return result;
}

void FontFallbackMap::InvalidateAll() {
  for (auto& entry : fallback_list_for_description_)
    if (auto list = entry.value.lock()) list->MarkInvalid();
  fallback_list_for_description_.clear();
}

template <typename Predicate>
void FontFallbackMap::InvalidateInternal(Predicate predicate) {
  Vector<FontDescription> invalidated;
  for (auto& entry : fallback_list_for_description_) {
    auto list = entry.value.lock();
    if (!list || predicate(*list)) {
      invalidated.push_back(entry.key);
      if (list) list->MarkInvalid();
    }
  }
  fallback_list_for_description_.RemoveAll(invalidated);
}

void FontFallbackMap::FontsNeedUpdate(FontSelector*,
                                      FontInvalidationReason reason) {
  switch (reason) {
  case FontInvalidationReason::kFontFaceLoaded:
    InvalidateInternal([](const FontFallbackList& fallback_list) {
      return fallback_list.HasLoadingFallback();
    });
    break;
  case FontInvalidationReason::kFontFaceDeleted:
    InvalidateInternal([](const FontFallbackList& fallback_list) {
      return fallback_list.HasCustomFont();
    });
    break;
  default:
    InvalidateAll();
  }
}

void FontFallbackMap::FontCacheInvalidated() {
  InvalidateAll();
}

} // namespace bkfont
