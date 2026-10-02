// Port source: third_party/blink/renderer/platform/fonts/opentype/font_format_check_test.cc
// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "font/font_format_check.h"

#include "gtest/gtest.h"
#include "support/font_test_helpers.h"

namespace blink {

class FontFormatCheckTest : public testing::Test {
protected:
  void EnsureFontData(String font_file_name) {
    auto font_file_data = test::ReadFromFile(test::PlatformTestDataPath(font_file_name));
    ASSERT_TRUE(font_file_data.has_value());
    ASSERT_FALSE(font_file_data->empty());
    font_data_ = std::move(*font_file_data);
  }

  Vector<char> font_data_;
};

TEST_F(FontFormatCheckTest, NoCOLR) {
  EnsureFontData("roboto-a.ttf");
  FontFormatCheck format_check(base::as_byte_span(font_data_));
  ASSERT_FALSE(format_check.IsColrCpalColorFontV0());
  ASSERT_FALSE(format_check.IsColrCpalColorFontV1());
}

TEST_F(FontFormatCheckTest, COLRV1) {
  EnsureFontData("colrv1_test.ttf");
  FontFormatCheck format_check(base::as_byte_span(font_data_));
  ASSERT_TRUE(format_check.IsColrCpalColorFontV1());
  ASSERT_FALSE(format_check.IsColrCpalColorFontV0());
}

TEST_F(FontFormatCheckTest, COLRV0) {
  EnsureFontData("colrv0_test.ttf");
  FontFormatCheck format_check(base::as_byte_span(font_data_));
  ASSERT_TRUE(format_check.IsColrCpalColorFontV0());
  ASSERT_FALSE(format_check.IsColrCpalColorFontV1());
}

} // namespace blink
