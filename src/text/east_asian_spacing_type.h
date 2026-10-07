// Ported from: blink/renderer/platform/text/east_asian_spacing_type.h
// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

namespace bkit {

// Represents the East Asian Spacing property, as defined in
// https://unicode.org/reports/tr59/.
enum class EastAsianSpacingType : uint8_t {
  kOther = 0,
  kNarrow,
  kConditional,
  kWide,
  // When adding values, ensure `CharacterProperty` has enough storage.
};

} // namespace bkit
