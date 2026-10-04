// Port source: third_party/blink/renderer/platform/fonts/font_fallback_iterator_test.cc
// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "font/font_fallback_iterator.h"

#include "gtest/gtest.h"
#include "font/font.h"
#include "font/font_fallback_priority.h"
#include "support/font_test_base.h"
#include "support/font_test_helpers.h"

using bkfont::test::CreateTestFont;

namespace bkfont {

const FontFallbackPriority FallbackPriorities[] = {
    FontFallbackPriority::kText,
    FontFallbackPriority::kEmojiText,
    FontFallbackPriority::kEmojiEmoji};

class TestReset : public testing::TestWithParam<FontFallbackPriority> {};

INSTANTIATE_TEST_SUITE_P(FontFallbackIteratorTest,
                         TestReset,
                         testing::ValuesIn(FallbackPriorities));

TEST_P(TestReset, TestResetWithFallbackPriority) {
  const FontFallbackPriority fallback_priorities = TestReset::GetParam();
  FontDescription::VariantLigatures ligatures(
      FontDescription::kDisabledLigaturesState);
  auto test_font =
      CreateTestFont(AtomicString("TestFont"),
                     test::PlatformTestDataPath("Ahem.woff"),
                     100,
                     &ligatures);

  FontFallbackIterator fallback_iterator =
      test_font->CreateFontFallbackIterator(fallback_priorities);
  FontFallbackIterator fallback_iterator_reset =
      test_font->CreateFontFallbackIterator(fallback_priorities);

  FontFallbackIterator::HintCharList fallback_chars_hint;
  fallback_iterator_reset.Next(fallback_chars_hint);
  fallback_iterator_reset.Reset();

  EXPECT_EQ(fallback_iterator_reset, fallback_iterator);
}

} // namespace bkfont
