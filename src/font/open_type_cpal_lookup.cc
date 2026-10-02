// Port source: third_party/blink/renderer/platform/fonts/opentype/open_type_cpal_lookup.cc
// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "open_type_cpal_lookup.h"

#include "font_table_harfbuzz.h"

// clang-format off
#include <hb.h>
#include <hb-cplusplus.hh>
#include <hb-ot.h>
// clang-format on

namespace {
uint32_t kCpalTag = HB_TAG('C', 'P', 'A', 'L');

} // namespace

#include "platform/font_face.h"
#include "font_table_harfbuzz.h"
namespace blink {

/* static */
std::optional<uint16_t> OpenTypeCpalLookup::FirstThemedPalette(
    std::shared_ptr<FontFace> typeface,
    PaletteUse palette_use) {
  if (!typeface || !typeface->HasTable(kCpalTag))
    return std::nullopt;

  hb::unique_ptr<hb_face_t> face(HbFaceFromFontFace(typeface));

  if (!face || !hb_ot_color_has_palettes(face.get()))
    return std::nullopt;

  int num_palettes = hb_ot_color_palette_get_count(face.get());

  const hb_ot_color_palette_flags_t desired_flag =
      palette_use == kUsableWithLightBackground
          ? HB_OT_COLOR_PALETTE_FLAG_USABLE_WITH_LIGHT_BACKGROUND
          : HB_OT_COLOR_PALETTE_FLAG_USABLE_WITH_DARK_BACKGROUND;
  for (int i = 0; i < num_palettes; ++i) {
    if (hb_ot_color_palette_get_flags(face.get(), i) == desired_flag)
      return i;
  }
  return std::nullopt;
}

Vector<Color> OpenTypeCpalLookup::RetrieveColorRecords(
    std::shared_ptr<FontFace> typeface,
    unsigned palette_index) {
  hb::unique_ptr<hb_face_t> face(HbFaceFromFontFace(typeface));

  if (!face) {
    return Vector<Color>();
  }

  unsigned num_colors = hb_ot_color_palette_get_colors(
      face.get(),
      palette_index,
      0,
      nullptr,
      nullptr);
  if (!num_colors) {
    return Vector<Color>();
  }

  std::unique_ptr<hb_color_t[]> colors =
      std::make_unique<hb_color_t[]>(num_colors);
  if (!hb_ot_color_palette_get_colors(face.get(), palette_index, 0, &num_colors, colors.get())) {
    return Vector<Color>();
  }
  Vector<Color> color_records(num_colors);
  for (unsigned i = 0; i < num_colors; i++) {
    color_records[i] =
        Color(hb_color_get_red(colors[i]), hb_color_get_green(colors[i]), hb_color_get_blue(colors[i]), hb_color_get_alpha(colors[i]));
  }
  return color_records;
}

} // namespace blink
