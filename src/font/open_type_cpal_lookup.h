// Port source: third_party/blink/renderer/platform/fonts/opentype/open_type_cpal_lookup.h
// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <optional>

#include "graphics/color.h"
#include "base/vector.h"

#include "platform/font_face.h"
#include "font_table_harfbuzz.h"
namespace bkfont {

/* Tools for inspecting the font palette of a COLR/CPAL font to find dark/light
 * mode preferred palettes and resolve string-based palette overrides as
 * specified in font-palette or @font-palette-values CSS. */
class OpenTypeCpalLookup {
public:
  enum PaletteUse {
    kUsableWithLightBackground,
    kUsableWithDarkBackground
  };

  /* Return the index of the first palette useful for the specified
   * palette use, dark or light. Important: The SkTypeface passed in
   * should allow efficient access to its internal data buffer using
   * SkTypeface::openStream, which is not the case for CoreText-backed
   * SkTypeface objects.
   */
  static std::optional<uint16_t> FirstThemedPalette(std::shared_ptr<FontFace> typeface,
                                                    PaletteUse palette_use);

  /* Returns a sorted Vector of color records from the specified font palette.
   * The position in the returned vector matches the palette index in the font.
   */
  static Vector<Color> RetrieveColorRecords(std::shared_ptr<FontFace> typeface,
                                            unsigned int palette_index);
};

} // namespace bkfont
