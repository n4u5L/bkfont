// Port source: third_party/blink/renderer/platform/fonts/font_family_test.cc
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "font/font_family.h"

#include "gtest/gtest.h"

namespace blink {

TEST(FontFamilyTest, ToString) {
  {
    FontFamily family;
    EXPECT_EQ("", family.ToString());
  }
  {
    std::shared_ptr<SharedFontFamily> b = SharedFontFamily::Create(
        AtomicString("B"),
        FontFamily::Type::kFamilyName);
    FontFamily family(AtomicString("A"), FontFamily::Type::kFamilyName, std::move(b));
    EXPECT_EQ("A, B", family.ToString());
  }
  {
    std::shared_ptr<SharedFontFamily> c = SharedFontFamily::Create(
        AtomicString("C"),
        FontFamily::Type::kFamilyName);
    std::shared_ptr<SharedFontFamily> b = SharedFontFamily::Create(
        AtomicString("B"),
        FontFamily::Type::kFamilyName,
        std::move(c));
    FontFamily family(AtomicString("A"), FontFamily::Type::kFamilyName, std::move(b));
    EXPECT_EQ("A, B, C", family.ToString());
  }
}

} // namespace blink
