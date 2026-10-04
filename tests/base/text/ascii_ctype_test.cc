// Source: third_party/blink/renderer/platform/wtf/text/ascii_ctype_test.cc
// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/text/ascii_ctype.h"

#include "gtest/gtest.h"

namespace bkfont {

TEST(ASCIICTypeTest, ASCIICaseFoldTable) {
  LChar symbol = 0xff;
  while (symbol--) {
    EXPECT_EQ(ToASCIILower<LChar>(symbol), kASCIICaseFoldTable[symbol]);
  }
}

TEST(ASCIICTypeTest, IsASCIISpaceWHATWG) {
  char c = 0xFF;
  do {
    bool expected_whitespace =
        c == 0x9 || c == 0xA || c == 0xC || c == 0xD || c == 0x20;
    EXPECT_EQ(IsASCIISpaceWHATWG(c), expected_whitespace);
  } while (c--);
}

} // namespace bkfont
