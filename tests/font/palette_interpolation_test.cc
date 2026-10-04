// Port source: third_party/blink/renderer/platform/fonts/palette_interpolation_test.cc
// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "font/palette_interpolation.h"

#include <memory>
// No asynchronous tasks are used by these native font tests.
#include "font/font.h"
#include "graphics/color.h"
#include "support/font_test_base.h"
#include "support/font_test_helpers.h"
#include "base/wtf_size_t.h"

#include <utility>
#include <vector>

namespace {

constexpr double kMaxAlphaDifference = 0.01;

bkfont::String pathToColorPalettesTestFont() {
  return bkfont::test::BlinkWebTestsDir() + "/external/wpt/css/css-fonts/resources/COLR-palettes-test-font.ttf";
}
bkfont::String pathToNonColorTestFont() {
  return bkfont::test::BlinkWebTestsFontsTestDataPath("Ahem.ttf");
}

} // namespace

namespace bkfont {

class PaletteInterpolationTest : public FontTestBase {
protected:
  std::shared_ptr<FontFace> LoadTypeface(const String& path) {
    if (!test::ReadFromFile(path))
      return nullptr;
    FontDescription::VariantLigatures ligatures;
    auto font = bkfont::test::CreateTestFont(AtomicString("Ahem"), path, 16, &ligatures);
    return font->PrimaryFont()->PlatformData().GetFontFace();
  }

  void ExpectColorsEqualInSRGB(
      Vector<FontPalette::FontPaletteOverride> overrides1,
      Vector<FontPalette::FontPaletteOverride> overrides2) {
    EXPECT_EQ(overrides1.size(), overrides2.size());
    for (wtf_size_t i = 0; i < overrides1.size(); i++) {
      EXPECT_EQ(overrides1[i].index, overrides2[i].index);
      Color color1 = overrides1[i].color;
      Color color2 = overrides2[i].color;
      EXPECT_EQ(DifferenceSquared(color1, color2), 0);
      // Due to the conversion from oklab to SRGB we should use epsilon
      // comparison.
      EXPECT_TRUE(std::fabs(color1.Alpha() - color2.Alpha()) < kMaxAlphaDifference);
    }
  }

  std::shared_ptr<FontFace> color_palette_typeface_;
  std::shared_ptr<FontFace> non_color_ahem_typeface_;
};

TEST_F(PaletteInterpolationTest, RetrievePaletteIndexFromColorFont) {
  color_palette_typeface_ = LoadTypeface(pathToColorPalettesTestFont());
  if (!color_palette_typeface_)
    GTEST_SKIP() << "Missing upstream font resource: " << pathToColorPalettesTestFont().Utf8();
  PaletteInterpolation palette_interpolation(color_palette_typeface_);
  std::shared_ptr<FontPalette> palette =
      FontPalette::Create(FontPalette::kDarkPalette);
  std::optional<uint16_t> index =
      palette_interpolation.RetrievePaletteIndex(palette.get());
  EXPECT_TRUE(index.has_value());
  EXPECT_EQ(*index, 3);
}

TEST_F(PaletteInterpolationTest, RetrievePaletteIndexFromNonColorFont) {
  non_color_ahem_typeface_ = LoadTypeface(pathToNonColorTestFont());
  if (!non_color_ahem_typeface_)
    GTEST_SKIP() << "Missing upstream font resource: " << pathToNonColorTestFont().Utf8();
  PaletteInterpolation palette_interpolation(non_color_ahem_typeface_);
  std::shared_ptr<FontPalette> palette =
      FontPalette::Create(FontPalette::kLightPalette);
  std::optional<uint16_t> index =
      palette_interpolation.RetrievePaletteIndex(palette.get());
  EXPECT_FALSE(index.has_value());
}

TEST_F(PaletteInterpolationTest, MixCustomPalettesAtHalfTime) {
  color_palette_typeface_ = LoadTypeface(pathToColorPalettesTestFont());
  if (!color_palette_typeface_)
    GTEST_SKIP() << "Missing upstream font resource: " << pathToColorPalettesTestFont().Utf8();
  PaletteInterpolation palette_interpolation(color_palette_typeface_);
  std::shared_ptr<FontPalette> palette_start =
      FontPalette::Create(AtomicString("palette1"));
  palette_start->SetBasePalette({FontPalette::kIndexBasePalette, 3});
  // palette_start has the following list of color records:
  // { rgba(255, 255, 0, 255) = oklab(96.8%, -17.75%, 49.75%),
  //   rgba(0, 0, 255, 255) = oklab(45.2%, -8%, -78%),
  //   rgba(255, 0, 255, 255) = oklab(70.2%, 68.75%, -42.25%),
  //   rgba(0, 255, 255, 255) = oklab(90.5%, -37.25%, -9.75%),
  //   rgba(255, 255, 255, 255) = oklab(100%, 0%, 0%),
  //   rgba(0, 0, 0, 255) = oklab(0%, 0%, 0%),
  //   rgba(255, 0, 0, 255) = oklab(62.8%, 56.25%, 31.5%),
  //   rgba(0, 255, 0, 255) = oklab(86.6%, -58.5%, 44.75%) }

  std::shared_ptr<FontPalette> palette_end =
      FontPalette::Create(AtomicString("palette2"));
  palette_end->SetBasePalette({FontPalette::kIndexBasePalette, 7});
  // palette_end has the following list of color records:
  // { rgba(255, 255, 255, 255) = oklab(100%, 0%, 0%),
  //   rgba(0, 0, 0, 255) = oklab(0%, 0%, 0%),
  //   rgba(255, 0, 0, 255) = oklab(62.8%, 56.25%, 31.5%),
  //   rgba(0, 255, 0, 255) = oklab(86.6%, -58.5%, 44.75%),
  //   rgba(255, 255, 0, 255) = oklab(96.8%, -17.75%, 49.75%),
  //   rgba(0, 0, 255, 255) = oklab(45.2%, -8%, -78%),
  //   rgba(255, 0, 255, 255) = oklab(70.2%, 68.75%, -42.25%),
  //   rgba(0, 255, 255, 255) = oklab(90.5%, -37.25%, -9.75%) }

  std::shared_ptr<FontPalette> palette =
      FontPalette::Mix(palette_start, palette_end, 50, 50, 0.5, 1.0, Color::ColorSpace::kOklab, std::nullopt);
  Vector<FontPalette::FontPaletteOverride> actual_color_records =
      palette_interpolation.ComputeInterpolableFontPalette(palette.get());
  // We expect each color to be half-way between palette_start and palette_end
  // after interpolation in the Oklab interpolation color space and conversion
  // back to sRGB.
  Vector<FontPalette::FontPaletteOverride> expected_color_records = {
      {0, Color::FromRGBA(254, 255, 172, 255)},
      {1, Color::FromRGBA(0, 0, 99, 255)},
      {2, Color::FromRGBA(253, 45, 155, 255)},
      {3, Color::FromRGBA(0, 255, 169, 255)},
      {4, Color::FromRGBA(254, 255, 172, 255)},
      {5, Color::FromRGBA(0, 0, 99, 255)},
      {6, Color::FromRGBA(253, 45, 155, 255)},
      {7, Color::FromRGBA(0, 255, 169, 255)},
  };
  ExpectColorsEqualInSRGB(actual_color_records, expected_color_records);
}

TEST_F(PaletteInterpolationTest, MixCustomAndNonExistingPalettes) {
  color_palette_typeface_ = LoadTypeface(pathToColorPalettesTestFont());
  if (!color_palette_typeface_)
    GTEST_SKIP() << "Missing upstream font resource: " << pathToColorPalettesTestFont().Utf8();
  PaletteInterpolation palette_interpolation(color_palette_typeface_);
  std::shared_ptr<FontPalette> palette_start =
      FontPalette::Create(AtomicString("palette1"));
  palette_start->SetBasePalette({FontPalette::kIndexBasePalette, 3});
  // palette_start has the following list of color records:
  // { rgba(255, 255, 0, 255) = oklab(96.8%, -17.75%, 49.75%),
  //   rgba(0, 0, 255, 255) = oklab(45.2%, -8%, -78%),
  //   rgba(255, 0, 255, 255) = oklab(70.2%, 68.75%, -42.25%),
  //   rgba(0, 255, 255, 255) = oklab(90.5%, -37.25%, -9.75%),
  //   rgba(255, 255, 255, 255) = oklab(100%, 0%, 0%),
  //   rgba(0, 0, 0, 255) = oklab(0%, 0%, 0%),
  //   rgba(255, 0, 0, 255) = oklab(62.8%, 56.25%, 31.5%),
  //   rgba(0, 255, 0, 255) = oklab(86.6%, -58.5%, 44.75%) }

  std::shared_ptr<FontPalette> palette_end =
      FontPalette::Create(AtomicString("palette2"));
  palette_end->SetBasePalette({FontPalette::kIndexBasePalette, 16});
  // Palette under index 16 does not exist, so instead normal palette is used.
  // Normal palette has the following list of color records:
  // { rgba(0, 0, 0, 255) = oklab(0%, 0%, 0%),
  //   rgba(255, 0, 0, 255) = oklab(62.8%, 56.25%, 31.5%),
  //   rgba(0, 255, 0, 255) = oklab(86.6%, -58.5%, 44.75%),
  //   rgba(255, 255, 0, 255) = oklab(96.8%, -17.75%, 49.75%),
  //   rgba(0, 0, 255, 255) = oklab(45.2%, -8%, -78%),
  //   rgba(255, 0, 255, 255) = oklab(70.2%, 68.75%, -42.25%),
  //   rgba(0, 255, 255, 255) = oklab(90.5%, -37.25%, -9.75%),
  //   rgba(255, 255, 255, 255) = oklab(100%, 0%, 0%) }

  std::shared_ptr<FontPalette> palette =
      FontPalette::Mix(palette_start, palette_end, 50, 50, 0.5, 1.0, Color::ColorSpace::kOklab, std::nullopt);
  Vector<FontPalette::FontPaletteOverride> actual_color_records =
      palette_interpolation.ComputeInterpolableFontPalette(palette.get());
  // We expect each color to be half-way between palette_start and normal
  // palette after interpolation in the Oklab interpolation color space and
  // conversion back to sRGB.
  Vector<FontPalette::FontPaletteOverride> expected_color_records = {
      {0, Color::FromRGBA(99, 99, 0, 255)},
      {1, Color::FromRGBA(140, 83, 162, 255)},
      {2, Color::FromRGBA(198, 180, 180, 255)},
      {3, Color::FromRGBA(176, 255, 176, 255)},
      {4, Color::FromRGBA(116, 163, 255, 255)},
      {5, Color::FromRGBA(99, 0, 99, 255)},
      {6, Color::FromRGBA(210, 169, 148, 255)},
      {7, Color::FromRGBA(173, 255, 166, 255)},
  };
  ExpectColorsEqualInSRGB(actual_color_records, expected_color_records);
}

TEST_F(PaletteInterpolationTest, MixNonExistingPalettes) {
  color_palette_typeface_ = LoadTypeface(pathToColorPalettesTestFont());
  if (!color_palette_typeface_)
    GTEST_SKIP() << "Missing upstream font resource: " << pathToColorPalettesTestFont().Utf8();
  PaletteInterpolation palette_interpolation(color_palette_typeface_);
  std::shared_ptr<FontPalette> palette_start =
      FontPalette::Create(AtomicString("palette1"));
  // Palette under index 16 does not exist, so instead normal palette is used.
  palette_start->SetBasePalette({FontPalette::kIndexBasePalette, 16});

  std::shared_ptr<FontPalette> palette_end =
      FontPalette::Create(AtomicString("palette2"));
  // Palette under index 17 does not exist, so instead normal palette is used.
  palette_end->SetBasePalette({FontPalette::kIndexBasePalette, 17});

  std::shared_ptr<FontPalette> palette =
      FontPalette::Mix(palette_start, palette_end, 50, 50, 0.5, 1.0, Color::ColorSpace::kOklab, std::nullopt);
  Vector<FontPalette::FontPaletteOverride> actual_color_records =
      palette_interpolation.ComputeInterpolableFontPalette(palette.get());
  // Since both of the endpoints are equal and have color records from normal
  // palette, we expect each colors from the normal palette in the result list.
  Vector<FontPalette::FontPaletteOverride> expected_color_records = {
      {0, Color::FromRGBA(0, 0, 0, 255)},
      {1, Color::FromRGBA(255, 0, 0, 255)},
      {2, Color::FromRGBA(0, 255, 0, 255)},
      {3, Color::FromRGBA(255, 255, 0, 255)},
      {4, Color::FromRGBA(0, 0, 255, 255)},
      {5, Color::FromRGBA(255, 0, 255, 255)},
      {6, Color::FromRGBA(0, 255, 255, 255)},
      {7, Color::FromRGBA(255, 255, 255, 255)},
  };
  ExpectColorsEqualInSRGB(actual_color_records, expected_color_records);
}

TEST_F(PaletteInterpolationTest, MixCustomPalettesInOklab) {
  color_palette_typeface_ = LoadTypeface(pathToColorPalettesTestFont());
  if (!color_palette_typeface_)
    GTEST_SKIP() << "Missing upstream font resource: " << pathToColorPalettesTestFont().Utf8();
  PaletteInterpolation palette_interpolation(color_palette_typeface_);
  std::shared_ptr<FontPalette> palette_start =
      FontPalette::Create(AtomicString("palette1"));
  palette_start->SetBasePalette({FontPalette::kIndexBasePalette, 3});
  // palette_start has the following list of color records:
  // { rgba(255, 255, 0, 255) = oklab(96.8%, -17.75%, 49.75%),
  //   rgba(0, 0, 255, 255) = oklab(45.2%, -8%, -78%),
  //   rgba(255, 0, 255, 255) = oklab(70.2%, 68.75%, -42.25%),
  //   rgba(0, 255, 255, 255) = oklab(90.5%, -37.25%, -9.75%),
  //   rgba(255, 255, 255, 255) = oklab(100%, 0%, 0%),
  //   rgba(0, 0, 0, 255) = oklab(0%, 0%, 0%),
  //   rgba(255, 0, 0, 255) = oklab(62.8%, 56.25%, 31.5%),
  //   rgba(0, 255, 0, 255) = oklab(86.6%, -58.5%, 44.75%) }

  std::shared_ptr<FontPalette> palette_end =
      FontPalette::Create(AtomicString("palette2"));
  palette_end->SetBasePalette({FontPalette::kIndexBasePalette, 7});
  // palette_end has the following list of color records:
  // { rgba(255, 255, 255, 255) = oklab(100%, 0%, 0%),
  //   rgba(0, 0, 0, 255) = oklab(0%, 0%, 0%),
  //   rgba(255, 0, 0, 255) = oklab(62.8%, 56.25%, 31.5%),
  //   rgba(0, 255, 0, 255) = oklab(86.6%, -58.5%, 44.75%),
  //   rgba(255, 255, 0, 255) = oklab(96.8%, -17.75%, 49.75%),
  //   rgba(0, 0, 255, 255) = oklab(45.2%, -8%, -78%),
  //   rgba(255, 0, 255, 255) = oklab(70.2%, 68.75%, -42.25%),
  //   rgba(0, 255, 255, 255) = oklab(90.5%, -37.25%, -9.75%) }

  std::shared_ptr<FontPalette> palette =
      FontPalette::Mix(palette_start, palette_end, 70, 30, 0.3, 1.0, Color::ColorSpace::kOklab, std::nullopt);
  Vector<FontPalette::FontPaletteOverride> actual_color_records =
      palette_interpolation.ComputeInterpolableFontPalette(palette.get());
  // We expect each color to be equal palette_start * 0.7 + palette_end * 0.3
  // after interpolation in the sRGB interpolation color space.
  Vector<FontPalette::FontPaletteOverride> expected_color_records = {
      {0, Color::FromRGBA(254, 255, 131, 255)},
      {1, Color::FromRGBA(0, 0, 158, 255)},
      {2, Color::FromRGBA(254, 42, 196, 255)},
      {3, Color::FromRGBA(0, 255, 205, 255)},
      {4, Color::FromRGBA(254, 255, 207, 255)},
      {5, Color::FromRGBA(0, 0, 46, 255)},
      {6, Color::FromRGBA(254, 39, 112, 255)},
      {7, Color::FromRGBA(0, 255, 128, 255)},
  };
  ExpectColorsEqualInSRGB(actual_color_records, expected_color_records);
}

TEST_F(PaletteInterpolationTest, MixCustomPalettesInSRGB) {
  color_palette_typeface_ = LoadTypeface(pathToColorPalettesTestFont());
  if (!color_palette_typeface_)
    GTEST_SKIP() << "Missing upstream font resource: " << pathToColorPalettesTestFont().Utf8();
  PaletteInterpolation palette_interpolation(color_palette_typeface_);
  std::shared_ptr<FontPalette> palette_start =
      FontPalette::Create(AtomicString("palette1"));
  palette_start->SetBasePalette({FontPalette::kIndexBasePalette, 3});
  // palette_start has the following list of color records:
  // { rgba(255, 255, 0, 255) = oklab(96.8%, -17.75%, 49.75%),
  //   rgba(0, 0, 255, 255) = oklab(45.2%, -8%, -78%),
  //   rgba(255, 0, 255, 255) = oklab(70.2%, 68.75%, -42.25%),
  //   rgba(0, 255, 255, 255) = oklab(90.5%, -37.25%, -9.75%),
  //   rgba(255, 255, 255, 255) = oklab(100%, 0%, 0%),
  //   rgba(0, 0, 0, 255) = oklab(0%, 0%, 0%),
  //   rgba(255, 0, 0, 255) = oklab(62.8%, 56.25%, 31.5%),
  //   rgba(0, 255, 0, 255) = oklab(86.6%, -58.5%, 44.75%) }

  std::shared_ptr<FontPalette> palette_end =
      FontPalette::Create(AtomicString("palette2"));
  palette_end->SetBasePalette({FontPalette::kIndexBasePalette, 7});
  // palette_end has the following list of color records:
  // { rgba(255, 255, 255, 255) = oklab(100%, 0%, 0%),
  //   rgba(0, 0, 0, 255) = oklab(0%, 0%, 0%),
  //   rgba(255, 0, 0, 255) = oklab(62.8%, 56.25%, 31.5%),
  //   rgba(0, 255, 0, 255) = oklab(86.6%, -58.5%, 44.75%),
  //   rgba(255, 255, 0, 255) = oklab(96.8%, -17.75%, 49.75%),
  //   rgba(0, 0, 255, 255) = oklab(45.2%, -8%, -78%),
  //   rgba(255, 0, 255, 255) = oklab(70.2%, 68.75%, -42.25%),
  //   rgba(0, 255, 255, 255) = oklab(90.5%, -37.25%, -9.75%) }

  std::shared_ptr<FontPalette> palette =
      FontPalette::Mix(palette_start, palette_end, 70, 30, 0.3, 1.0, Color::ColorSpace::kSRGB, std::nullopt);
  Vector<FontPalette::FontPaletteOverride> actual_color_records =
      palette_interpolation.ComputeInterpolableFontPalette(palette.get());
  // We expect each color to be equal palette_start * 0.7 + palette_end * 0.3
  // after interpolation in the Oklab interpolation color space and conversion
  // back to sRGB.
  Vector<FontPalette::FontPaletteOverride> expected_color_records = {
      {0, Color::FromRGBA(255, 255, 77, 255)},
      {1, Color::FromRGBA(0, 0, 179, 255)},
      {2, Color::FromRGBA(255, 0, 179, 255)},
      {3, Color::FromRGBA(0, 255, 179, 255)},
      {4, Color::FromRGBA(255, 255, 179, 255)},
      {5, Color::FromRGBA(0, 0, 77, 255)},
      {6, Color::FromRGBA(255, 0, 77, 255)},
      {7, Color::FromRGBA(0, 255, 77, 255)},
  };
  ExpectColorsEqualInSRGB(actual_color_records, expected_color_records);
}

} // namespace bkfont
