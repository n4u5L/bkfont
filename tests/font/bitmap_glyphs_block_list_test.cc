// Port source: third_party/blink/renderer/platform/fonts/bitmap_glyphs_block_list_test.cc
// The block-list decision is exposed through the native platform render options.
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "font/font_platform_data.h"
#include "font/font_cache.h"

#include "build/build_config.h"
#include "support/font_test_base.h"

namespace blink {

#if BUILDFLAG(IS_WIN)

class BlockListBitmapGlyphsTest : public FontTestBase {};

static void TestBitmapGlyphsBlockListed(AtomicString windows_family_name,
                                        bool block_listed_expected) {
  FontCache& font_cache = FontCache::Get();
  FontDescription font_description;
  font_description.SetFamily(FontFamily(
      windows_family_name,
      FontFamily::InferredTypeFor(windows_family_name)));
  auto simple_font_data =
      font_cache.GetFontData(font_description, windows_family_name);
  ASSERT_TRUE(simple_font_data);
  const FontPlatformData& font_platform_data = simple_font_data->PlatformData();
  ASSERT_TRUE(font_platform_data.GetFontFace());
  ASSERT_EQ(block_listed_expected,
            !font_platform_data.RenderOptions().embedded_bitmaps);
}

TEST_F(BlockListBitmapGlyphsTest, Simsun) {
  TestBitmapGlyphsBlockListed(AtomicString("Simsun"), false);
}

TEST_F(BlockListBitmapGlyphsTest, Arial) {
  TestBitmapGlyphsBlockListed(AtomicString("Arial"), false);
}

TEST_F(BlockListBitmapGlyphsTest, Calibri) {
  TestBitmapGlyphsBlockListed(AtomicString("Calibri"), true);
}

#endif
} // namespace blink
