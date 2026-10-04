// Port source: third_party/blink/renderer/platform/fonts/typesetting_features_test.cc
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "font/typesetting_features.h"

#include "gtest/gtest.h"
#include "base/text/wtf_string.h"

namespace bkfont {

TEST(TypesettingFeaturesTest, ToString) {
  {
    TypesettingFeatures features = 0;
    EXPECT_EQ("", ToString(features));
  }
  {
    TypesettingFeatures features = kKerning | kLigatures;
    EXPECT_EQ("Kerning,Ligatures", ToString(features));
  }
  {
    TypesettingFeatures features = kKerning | kLigatures | kCaps;
    EXPECT_EQ("Kerning,Ligatures,Caps", ToString(features));
  }
}

} // namespace bkfont
