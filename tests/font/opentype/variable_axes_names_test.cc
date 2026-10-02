// Port source: third_party/blink/renderer/platform/fonts/opentype/variable_axes_names_test.cc
// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "font/variable_axes_names.h"
#include "platform/font_manager.h"

#include "gtest/gtest.h"
#include "support/font_test_helpers.h"

namespace blink {

TEST(VariableAxesNamesTest, TestVariableAxes) {
  String file_path = blink::test::BlinkWebTestsDir() + "/third_party/Homecomputer/Sixtyfour.ttf";
  if (!test::ReadFromFile(file_path))
    GTEST_SKIP() << "Missing upstream font resource: " << file_path.Utf8();
  auto mgr = FontManager::Create();
  auto typeface = mgr->CreateFromFile(file_path, 0);
  Vector<VariationAxis> axes = VariableAxesNames::GetVariationAxes(typeface);
  EXPECT_EQ(axes.size(), (unsigned)2);
  VariationAxis axis1 = axes.at(0);
  EXPECT_EQ(axis1.name, "Weight");
  EXPECT_EQ(axis1.tag, "wght");
  EXPECT_EQ(axis1.minValue, 200);
  EXPECT_EQ(axis1.maxValue, 900);
  EXPECT_EQ(axis1.defaultValue, 200);
  VariationAxis axis2 = axes.at(1);
  EXPECT_EQ(axis2.name, "Width");
  EXPECT_EQ(axis2.tag, "wdth");
  EXPECT_EQ(axis2.minValue, 100);
  EXPECT_EQ(axis2.maxValue, 200);
  EXPECT_EQ(axis2.defaultValue, 100);
}

} // namespace blink
