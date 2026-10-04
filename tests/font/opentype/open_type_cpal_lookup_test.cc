// Port source: third_party/blink/renderer/platform/fonts/opentype/open_type_cpal_lookup_test.cc
// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "font/open_type_cpal_lookup.h"

#include <memory>
#include "font/font.h"
#include "support/font_test_base.h"
#include "support/font_test_helpers.h"

#include <utility>
#include <vector>

namespace {
bkfont::String pathToColrPalettesTestFont() {
  return bkfont::test::BlinkWebTestsDir() + "/external/wpt/css/css-fonts/resources/COLR-palettes-test-font.ttf";
}
bkfont::String pathToNonColrTestFont() {
  return bkfont::test::BlinkWebTestsFontsTestDataPath("Ahem.ttf");
}
} // namespace

namespace bkfont {

class OpenTypeCpalLookupTest : public FontTestBase {
protected:
  std::shared_ptr<FontFace> LoadTypeface(const String& path) {
    if (!test::ReadFromFile(path))
      return nullptr;
    FontDescription::VariantLigatures ligatures;
    auto font = bkfont::test::CreateTestFont(AtomicString("Ahem"), path, 16, &ligatures);
    return font->PrimaryFont()->PlatformData().GetFontFace();
  }

  std::shared_ptr<FontFace> colr_palette_typeface_;
  std::shared_ptr<FontFace> non_colr_ahem_typeface_;
};

TEST_F(OpenTypeCpalLookupTest, NoResultForNonColr) {
  non_colr_ahem_typeface_ = LoadTypeface(pathToNonColrTestFont());
  if (!non_colr_ahem_typeface_)
    GTEST_SKIP() << "Missing upstream font resource: " << pathToNonColrTestFont().Utf8();
  for (auto& palette_use : {OpenTypeCpalLookup::kUsableWithLightBackground,
                            OpenTypeCpalLookup::kUsableWithDarkBackground}) {
    std::optional<uint16_t> palette_result =
        OpenTypeCpalLookup::FirstThemedPalette(non_colr_ahem_typeface_,
                                               palette_use);
    EXPECT_FALSE(palette_result.has_value());
  }
}

TEST_F(OpenTypeCpalLookupTest, DarkLightPalettes) {
  colr_palette_typeface_ = LoadTypeface(pathToColrPalettesTestFont());
  if (!colr_palette_typeface_)
    GTEST_SKIP() << "Missing upstream font resource: " << pathToColrPalettesTestFont().Utf8();
  // COLR-palettes-test-font.tff dumped with FontTools has
  //     <palette index="2" type="1">[...]
  //     <palette index="3" type="2">
  // meaning palette index 2 is the first palette usable for light backgrounds,
  // and palette index 3 is the first palette usable for dark background.
  std::vector<std::pair<OpenTypeCpalLookup::PaletteUse, uint16_t>> expectations{
      {OpenTypeCpalLookup::kUsableWithLightBackground, 2},
      {OpenTypeCpalLookup::kUsableWithDarkBackground, 3}};
  for (auto& expectation : expectations) {
    std::optional<uint16_t> palette_result =
        OpenTypeCpalLookup::FirstThemedPalette(colr_palette_typeface_,
                                               expectation.first);
    EXPECT_TRUE(palette_result.has_value());
    EXPECT_EQ(*palette_result, expectation.second);
  }
}

TEST_F(OpenTypeCpalLookupTest, RetrieveColorRecordsFromExistingPalette) {
  colr_palette_typeface_ = LoadTypeface(pathToColrPalettesTestFont());
  if (!colr_palette_typeface_)
    GTEST_SKIP() << "Missing upstream font resource: " << pathToColrPalettesTestFont().Utf8();
  Vector<Color> expected_color_records = {
      Color::FromRGBA(255, 255, 0, 255),
      Color::FromRGBA(0, 0, 255, 255),
      Color::FromRGBA(255, 0, 255, 255),
      Color::FromRGBA(0, 255, 255, 255),
      Color::FromRGBA(255, 255, 255, 255),
      Color::FromRGBA(0, 0, 0, 255),
      Color::FromRGBA(255, 0, 0, 255),
      Color::FromRGBA(0, 255, 0, 255),
  };

  Vector<Color> actual_color_records =
      OpenTypeCpalLookup::RetrieveColorRecords(colr_palette_typeface_, 3);

  EXPECT_EQ(expected_color_records, actual_color_records);
}

TEST_F(OpenTypeCpalLookupTest, RetrieveColorRecordsFromNonExistingPalette) {
  colr_palette_typeface_ = LoadTypeface(pathToColrPalettesTestFont());
  if (!colr_palette_typeface_)
    GTEST_SKIP() << "Missing upstream font resource: " << pathToColrPalettesTestFont().Utf8();
  // Palette at index 16 does not exist in the font should return empty Vector
  Vector<Color> actual_color_records =
      OpenTypeCpalLookup::RetrieveColorRecords(colr_palette_typeface_, 16);

  EXPECT_EQ(actual_color_records.size(), 0u);
}

} // namespace bkfont
