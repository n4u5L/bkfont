// Ported from: blink/renderer/platform/fonts/font_selector_client.h
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "font_invalidation_reason.h"

#include <memory>
#include "base/hash_map.h"
namespace bkfont {

class FontSelector;

class FontSelectorClient {
public:
  virtual ~FontSelectorClient() = default;

  virtual void FontsNeedUpdate(FontSelector*, FontInvalidationReason) = 0;
};

} // namespace bkfont
