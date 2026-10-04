// Port source: third_party/blink/renderer/platform/fonts/palette_interpolation.h
// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <optional>

#include "font_palette.h"
#include "graphics/color.h"

#include "platform/font_face.h"
#include "font_table_harfbuzz.h"
namespace bkfont {

class PaletteInterpolation {
public:
  explicit PaletteInterpolation(std::shared_ptr<FontFace> typeface)
      : typeface_(typeface) {
  }
  std::optional<uint16_t> RetrievePaletteIndex(
      const FontPalette* palette) const;
  Vector<FontPalette::FontPaletteOverride> ComputeInterpolableFontPalette(
      const FontPalette* palette) const;

private:
  Vector<FontPalette::FontPaletteOverride> RetrieveColorRecords(
      const FontPalette* palette,
      unsigned int palette_index) const;
  static Vector<FontPalette::FontPaletteOverride> MixColorRecords(
      Vector<FontPalette::FontPaletteOverride>&& start_color_records,
      Vector<FontPalette::FontPaletteOverride>&& end_color_records,
      double percentage,
      double alpha_multiplier,
      Color::ColorSpace color_interpolation_space,
      std::optional<Color::HueInterpolationMethod> hue_interpolation_method);
  std::shared_ptr<FontFace> typeface_;
};

} // namespace bkfont
