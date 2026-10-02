// Port source: third_party/blink/renderer/platform/fonts/opentype/color_table_lookup_test.cc
// Excluded cases: TestMonoFont, TestFontWithColrTable. The production table lookup
// is private to harfbuzz_face.cc; there is no public ColorTableLookup API to test.
// Do not copy the lookup algorithm into this test and test the copy.
#if 0
// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/fonts/opentype/color_table_lookup.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/renderer/platform/fonts/font.h"
#include "third_party/blink/renderer/platform/testing/font_test_base.h"
#include "third_party/blink/renderer/platform/testing/font_test_helpers.h"
#include "third_party/blink/renderer/platform/testing/unit_test_helpers.h"

namespace blink {

class ColorTableLookupTest : public FontTestBase {};

TEST_F(ColorTableLookupTest, TestMonoFont) {
  Font* font = blink::test::CreateTestFont(
      AtomicString("Ahem"), blink::test::PlatformTestDataPath("Ahem.woff"), 11);
  const SkTypeface* typeface = font->PrimaryFont()->PlatformData().Typeface();
  EXPECT_FALSE(ColorTableLookup::TypefaceHasAnySupportedColorTable(typeface));
}

TEST_F(ColorTableLookupTest, TestFontWithColrTable) {
  Font* font = blink::test::CreateTestFont(
      AtomicString("NotoColorEmoji"),
      blink::test::BlinkWebTestsDir() +
          "/third_party/NotoColorEmoji/NotoColorEmoji.ttf",
      11);
  const SkTypeface* typeface = font->PrimaryFont()->PlatformData().Typeface();
  EXPECT_TRUE(ColorTableLookup::TypefaceHasAnySupportedColorTable(typeface));
}

}  // namespace blink

#endif
