// Port source: third_party/blink/renderer/platform/fonts/font_palette_test.cc
// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "font/font_palette.h"

#include <memory>
#include "gtest/gtest.h"

namespace bkfont {

TEST(FontPaletteTest, HashingAndComparison) {
  std::shared_ptr<FontPalette> a = FontPalette::Create();

  std::shared_ptr<FontPalette> b =
      FontPalette::Create(FontPalette::kLightPalette);
  EXPECT_NE(a->GetHash(), b->GetHash());
  EXPECT_NE(a, b);

  b = FontPalette::Create(FontPalette::kDarkPalette);
  EXPECT_NE(a->GetHash(), b->GetHash());
  EXPECT_NE(a, b);

  b = FontPalette::Create(AtomicString("SomePaletteReference"));
  EXPECT_NE(a->GetHash(), b->GetHash());
  EXPECT_NE(a, b);

  b = FontPalette::Mix(FontPalette::Create(FontPalette::kLightPalette),
                       FontPalette::Create(FontPalette::kDarkPalette),
                       30,
                       70,
                       0.7,
                       1.0,
                       Color::ColorSpace::kSRGB,
                       std::nullopt);
  EXPECT_NE(a->GetHash(), b->GetHash());
  EXPECT_NE(a, b);

  std::shared_ptr<FontPalette> c =
      FontPalette::Mix(FontPalette::Create(FontPalette::kLightPalette),
                       FontPalette::Create(FontPalette::kDarkPalette),
                       15,
                       35,
                       0.7,
                       1.0,
                       Color::ColorSpace::kSRGB,
                       std::nullopt);
  EXPECT_NE(c->GetHash(), b->GetHash());
  EXPECT_NE(c, b);

  c = FontPalette::Mix(FontPalette::Create(FontPalette::kLightPalette),
                       FontPalette::Create(),
                       30,
                       70,
                       0.7,
                       1.0,
                       Color::ColorSpace::kSRGB,
                       std::nullopt);
  EXPECT_NE(c->GetHash(), b->GetHash());
  EXPECT_NE(c, b);

  c = FontPalette::Mix(FontPalette::Create(FontPalette::kLightPalette),
                       FontPalette::Create(FontPalette::kDarkPalette),
                       30,
                       70,
                       0.7,
                       1.0,
                       Color::ColorSpace::kOklab,
                       std::nullopt);
  EXPECT_NE(c->GetHash(), b->GetHash());
  EXPECT_NE(c, b);
}

TEST(FontPaletteTest, MixPaletteValue) {
  std::shared_ptr<FontPalette> palette =
      FontPalette::Mix(FontPalette::Create(FontPalette::kLightPalette),
                       FontPalette::Create(FontPalette::kDarkPalette),
                       30,
                       70,
                       0.7,
                       1.0,
                       Color::ColorSpace::kSRGB,
                       std::nullopt);
  EXPECT_EQ("palette-mix(in srgb, light, dark 70%)", palette->ToString());
}

TEST(FontPaletteTest, NestedMixPaletteValue) {
  std::shared_ptr<FontPalette> palette_start = FontPalette::Create();
  std::shared_ptr<FontPalette> palette_end =
      FontPalette::Mix(FontPalette::Create(FontPalette::kLightPalette),
                       FontPalette::Create(FontPalette::kDarkPalette),
                       70,
                       30,
                       0.3,
                       1.0,
                       Color::ColorSpace::kSRGB,
                       std::nullopt);
  std::shared_ptr<FontPalette> palette =
      FontPalette::Mix(palette_start, palette_end, 30, 70, 0.7, 1.0, Color::ColorSpace::kOklab, std::nullopt);
  EXPECT_EQ(
      "palette-mix(in oklab, normal, palette-mix(in srgb, light, dark 30%) "
      "70%)",
      palette->ToString());
}

TEST(FontPaletteTest, InterpolablePalettesNotEqual) {
  std::shared_ptr<FontPalette> palette1 =
      FontPalette::Mix(FontPalette::Create(FontPalette::kDarkPalette),
                       FontPalette::Create(FontPalette::kLightPalette),
                       90,
                       10,
                       0.1,
                       1.0,
                       Color::ColorSpace::kOklab,
                       std::nullopt);
  std::shared_ptr<FontPalette> palette2 = FontPalette::Mix(
      FontPalette::Create(FontPalette::kDarkPalette),
      FontPalette::Create(),
      90,
      10,
      0.1,
      1.0,
      Color::ColorSpace::kOklab,
      std::nullopt);
  EXPECT_FALSE(*palette1.get() == *palette2.get());
}

TEST(FontPaletteTest, InterpolableAndNonInterpolablePalettesNotEqual) {
  std::shared_ptr<FontPalette> palette1 =
      FontPalette::Create(FontPalette::kDarkPalette);
  std::shared_ptr<FontPalette> palette2 =
      FontPalette::Mix(FontPalette::Create(FontPalette::kDarkPalette),
                       FontPalette::Create(FontPalette::kLightPalette),
                       90,
                       10,
                       0.1,
                       1.0,
                       Color::ColorSpace::kSRGB,
                       std::nullopt);
  EXPECT_FALSE(*palette1.get() == *palette2.get());
}

TEST(FontPaletteTest, NonInterpolablePalettesNotEqual) {
  std::shared_ptr<FontPalette> palette1 =
      FontPalette::Create(FontPalette::kDarkPalette);
  palette1->SetMatchFamilyName(AtomicString("family1"));
  std::shared_ptr<FontPalette> palette2 =
      FontPalette::Create(FontPalette::kDarkPalette);
  palette1->SetMatchFamilyName(AtomicString("family2"));
  EXPECT_FALSE(*palette1.get() == *palette2.get());
}

TEST(FontPaletteTest, PalettesEqual) {
  std::shared_ptr<FontPalette> palette1 =
      FontPalette::Mix(FontPalette::Create(FontPalette::kDarkPalette),
                       FontPalette::Create(FontPalette::kLightPalette),
                       90,
                       10,
                       0.1,
                       1.0,
                       Color::ColorSpace::kOklab,
                       std::nullopt);
  std::shared_ptr<FontPalette> palette2 =
      FontPalette::Mix(FontPalette::Create(FontPalette::kDarkPalette),
                       FontPalette::Create(FontPalette::kLightPalette),
                       90,
                       10,
                       0.1,
                       1.0,
                       Color::ColorSpace::kOklab,
                       std::nullopt);
  EXPECT_TRUE(*palette1.get() == *palette2.get());
}

TEST(FontPaletteTest, ComputeEndpointPercentagesFromNormalized) {
  FontPalette::NonNormalizedPercentages expected_percentages_1({50, 50});
  FontPalette::NonNormalizedPercentages actual_percentages_1 =
      FontPalette::ComputeEndpointPercentagesFromNormalized(0.5);

  FontPalette::NonNormalizedPercentages expected_percentages_2({70, 30});
  FontPalette::NonNormalizedPercentages actual_percentages_2 =
      FontPalette::ComputeEndpointPercentagesFromNormalized(0.3);

  FontPalette::NonNormalizedPercentages expected_percentages_3({0, 100});
  FontPalette::NonNormalizedPercentages actual_percentages_3 =
      FontPalette::ComputeEndpointPercentagesFromNormalized(1.0);

  EXPECT_EQ(expected_percentages_1, actual_percentages_1);
  EXPECT_EQ(expected_percentages_2, actual_percentages_2);
  EXPECT_EQ(expected_percentages_3, actual_percentages_3);
}

} // namespace bkfont
