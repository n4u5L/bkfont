// Ported from: blink/renderer/platform/text/han_kerning_char_type.h
// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

namespace bkit {

//
// Character types for the `HanKerning` class.
//
// https://drafts.csswg.org/css-text-4/#text-spacing-classes
//
enum class HanKerningCharType : uint8_t {
  kOther,
  kOpen,
  kClose,
  kMiddle,

  // Unicode General Category `Ps` and `Pe` that are not fullwidth. They are not
  // in the "Text Spacing Character Classes", but the "Fullwidth Punctuation
  // Collapsing" has them.
  // https://drafts.csswg.org/css-text-4/#fullwidth-collapsing
  kOpenNarrow,
  kCloseNarrow,

  // Following types depend on fonts. `HanKerning::GetCharType()` can resolve
  // them to types above.
  kDot,
  kColon,
  kSemicolon,
  kOpenQuote,
  kCloseQuote,

  // This value is used only during the computation, and therefore
  // `CharacterProperty` doesn't have to count for it.
  kInvalid,

  // When adding values, ensure `CharacterProperty` has enough storage. Also see
  // `kInvalid`.
};

} // namespace bkit
