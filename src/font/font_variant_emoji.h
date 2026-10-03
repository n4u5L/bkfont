// Port source: third_party/blink/renderer/platform/fonts/font_variant_emoji.h
// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "base/text/wtf_string.h"

#include <cstdint>
#include "base/text/wtf_string.h"

namespace blink {
enum FontVariantEmoji {
  kNormalVariantEmoji,
  kTextVariantEmoji,
  kEmojiVariantEmoji,
  kUnicodeVariantEmoji
};

String ToString(FontVariantEmoji);
} // namespace blink
