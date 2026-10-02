// Source: third_party/blink/renderer/platform/text/character_property.h
// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>

namespace blink {

using CharacterPropertyType = uint16_t;

// TODO(lingqi): Some values are never used. e.g., a char that is Hangul should
// always be kIsCJKIdeographOrSymbol, os 0b1000 is never used. Attempt to
// compress the Property.
enum class CharacterProperty : CharacterPropertyType {
  kIsCJKIdeographOrSymbol = 1 << 0,
  kIsPotentialCustomElementNameChar = 1 << 1,
  kIsBidiControl = 1 << 2,
  kIsHangul = 1 << 3,

  // Bits to store `HanKerningCharType`.
  kHanKerningShift = 4,
  kHanKerningSize = 4,
  kHanKerningMask = ((1 << kHanKerningSize) - 1),
  kHanKerningShiftedMask = kHanKerningMask << kHanKerningShift,

  // Bits to store `EastAsianSpacingType`.
  kEastAsianSpacingShift = 8,
  kEastAsianSpacingSize = 2,
  kEastAsianSpacingShiftedMask = ((1 << kEastAsianSpacingSize) - 1)
                                 << kEastAsianSpacingShift,
  kNumBits = kEastAsianSpacingShift + kEastAsianSpacingSize,
};

inline CharacterProperty operator|(CharacterProperty a, CharacterProperty b) {
  return static_cast<CharacterProperty>(static_cast<CharacterPropertyType>(a) | static_cast<CharacterPropertyType>(b));
}

inline CharacterProperty operator&(CharacterProperty a, CharacterProperty b) {
  return static_cast<CharacterProperty>(static_cast<CharacterPropertyType>(a) & static_cast<CharacterPropertyType>(b));
}

inline CharacterProperty operator|=(CharacterProperty& a, CharacterProperty b) {
  a = a | b;
  return a;
}

} // namespace blink
