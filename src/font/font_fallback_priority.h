// Ported from: blink/renderer/platform/fonts/font_fallback_priority.h
// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include "base/text/wtf_string.h"

namespace bkit {

// http://unicode.org/reports/tr51/#Presentation_Style discusses the differences
// between emoji in text and the emoji in emoji presentation. In that sense, the
// EmojiEmoji wording is taken from there.  Also compare
// http://unicode.org/Public/emoji/1.0/emoji-data.txt
enum class FontFallbackPriority {
  // For regular non-symbols text,
  // normal text fallback in FontFallbackIterator
  kText,
  // For emoji in text presentation
  kEmojiText,
  // For emoji in text presentation with variation selector 15
  kEmojiTextWithVS,
  // For emoji in emoji presentation
  kEmojiEmoji,
  // For emoji in emoji presentation with variation selector 16
  kEmojiEmojiWithVS,
  kInvalid,

  // When adding values, ensure `kMaxEnumValue` is the largest value to store
  // (values that can be returned for non-empty inputs).
  kMaxEnumValue = kEmojiEmojiWithVS,
};

inline bool IsNonTextFallbackPriority(FontFallbackPriority fallback_priority) {
  return fallback_priority == FontFallbackPriority::kEmojiText || fallback_priority == FontFallbackPriority::kEmojiEmoji || fallback_priority == FontFallbackPriority::kEmojiTextWithVS || fallback_priority == FontFallbackPriority::kEmojiEmojiWithVS;
}

inline bool HasVSFallbackPriority(FontFallbackPriority fallback_priority) {
  return fallback_priority == FontFallbackPriority::kEmojiTextWithVS || fallback_priority == FontFallbackPriority::kEmojiEmojiWithVS;
}

inline bool IsEmojiPresentationEmoji(FontFallbackPriority fallback_priority) {
  return fallback_priority == FontFallbackPriority::kEmojiEmoji || fallback_priority == FontFallbackPriority::kEmojiEmojiWithVS;
}

inline bool IsTextPresentationEmoji(FontFallbackPriority fallback_priority) {
  return fallback_priority == FontFallbackPriority::kEmojiText || fallback_priority == FontFallbackPriority::kEmojiTextWithVS;
}

} // namespace bkit
