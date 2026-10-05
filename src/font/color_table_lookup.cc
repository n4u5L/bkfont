// Ported from: blink/renderer/platform/fonts/opentype/color_table_lookup.cc

// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "color_table_lookup.h"

#include <cstdint>
#include <memory>
#include <span>

namespace bkfont {

namespace {

constexpr std::uint32_t MakeTag(char a, char b, char c, char d) {
  return (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(b) << 16) | (static_cast<std::uint32_t>(c) << 8) | static_cast<std::uint32_t>(d);
}

constexpr std::uint32_t kCpalTag = MakeTag('C', 'P', 'A', 'L');
constexpr std::uint32_t kColrTag = MakeTag('C', 'O', 'L', 'R');
constexpr std::uint32_t kSbixTag = MakeTag('s', 'b', 'i', 'x');
constexpr std::uint32_t kCbdtTag = MakeTag('C', 'B', 'D', 'T');
constexpr std::uint32_t kCblcTag = MakeTag('C', 'B', 'L', 'C');

} // namespace

bool ColorTableLookup::TypefaceHasAnySupportedColorTable(
    const Typeface* typeface) {
  if (!typeface) {
    return false;
  }
  const int num_tags = typeface->CountTables();
  if (!num_tags) {
    return false;
  }
  auto tags = std::make_unique<std::uint32_t[]>(static_cast<std::size_t>(num_tags));
  const int returned_tags = typeface->ReadTableTags(std::span(tags.get(), static_cast<std::size_t>(num_tags)));
  if (!returned_tags) {
    return false;
  }
  bool has_cpal = false;
  bool has_colr = false;
  bool has_cbdt = false;
  bool has_cblc = false;
  for (int i = 0; i < returned_tags; i++) {
    std::uint32_t tag = tags[i];
    if (tag == kSbixTag) {
      return true;
    }
    if (tag == kCpalTag) {
      if (has_colr) {
        return true;
      }
      has_cpal = true;
    } else if (tag == kColrTag) {
      if (has_cpal) {
        return true;
      }
      has_colr = true;
    } else if (tag == kCbdtTag) {
      if (has_cblc) {
        return true;
      }
      has_cbdt = true;
    } else if (tag == kCblcTag) {
      if (has_cbdt) {
        return true;
      }
      has_cblc = true;
    }
  }
  return false;
}

} // namespace bkfont
