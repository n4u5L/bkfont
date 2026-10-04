// Port source: third_party/blink/renderer/platform/fonts/opentype/font_settings_test.cc
// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "font/font_settings.h"

#include "base/compiler_specific.h"
#include <memory>
#include "gtest/gtest.h"

namespace bkfont {

namespace {

template <typename T, typename U>
std::shared_ptr<T> MakeSettings(std::initializer_list<U> items) {
  std::shared_ptr<T> settings = T::Create();
  for (auto item = items.begin(); item != items.end(); ++item) {
    settings->Append(*item);
  }
  return settings;
}

} // namespace

TEST(FontSettingsTest, HashTest) {
  std::shared_ptr<FontVariationSettings> one_axis_a =
      MakeSettings<FontVariationSettings, FontVariationAxis>(
          {FontVariationAxis{AtomicString("a   "), 0}});
  std::shared_ptr<FontVariationSettings> one_axis_b =
      MakeSettings<FontVariationSettings, FontVariationAxis>(
          {FontVariationAxis{AtomicString("b   "), 0}});
  std::shared_ptr<FontVariationSettings> two_axes =
      MakeSettings<FontVariationSettings, FontVariationAxis>(
          {FontVariationAxis{AtomicString("a   "), 0},
           FontVariationAxis{AtomicString("b   "), 0}});
  std::shared_ptr<FontVariationSettings> two_axes_different_value =
      MakeSettings<FontVariationSettings, FontVariationAxis>(
          {FontVariationAxis{AtomicString("a   "), 0},
           FontVariationAxis{AtomicString("b   "), 1}});

  std::shared_ptr<FontVariationSettings> empty_variation_settings =
      FontVariationSettings::Create();

  ASSERT_NE(one_axis_a->GetHash(), one_axis_b->GetHash());
  ASSERT_NE(one_axis_a->GetHash(), two_axes->GetHash());
  ASSERT_NE(one_axis_a->GetHash(), two_axes_different_value->GetHash());
  ASSERT_NE(empty_variation_settings->GetHash(), one_axis_a->GetHash());
  ASSERT_EQ(empty_variation_settings->GetHash(), 0u);
}

TEST(FontSettingsTest, ToString) {
  {
    std::shared_ptr<FontVariationSettings> settings =
        MakeSettings<FontVariationSettings, FontVariationAxis>(
            {FontVariationAxis{AtomicString("aaaa"), 42},
             FontVariationAxis{AtomicString("bbbb"), 8118}});
    EXPECT_EQ("aaaa=42,bbbb=8118", settings->ToString());
  }
  {
    std::shared_ptr<FontFeatureSettings> settings =
        MakeSettings<FontFeatureSettings, FontFeature>(
            {FontFeature{AtomicString("aaaa"), 42},
             FontFeature{AtomicString("bbbb"), 8118}});
    EXPECT_EQ("aaaa=42,bbbb=8118", settings->ToString());
  }
}
TEST(FontSettingsTest, FindTest) {
  {
    std::shared_ptr<FontVariationSettings> settings =
        MakeSettings<FontVariationSettings, FontVariationAxis>(
            {FontVariationAxis{AtomicString("abcd"), 42},
             FontVariationAxis{AtomicString("efgh"), 8118}});
    FontVariationAxis found_axis(0, 0);
    ASSERT_FALSE(settings->FindPair('aaaa', &found_axis));
    ASSERT_FALSE(settings->FindPair('bbbb', &found_axis));
    ASSERT_EQ(found_axis.Value(), 0);
    ASSERT_TRUE(settings->FindPair('abcd', &found_axis));
    ASSERT_EQ(found_axis.TagString(), AtomicString("abcd"));
    ASSERT_EQ(found_axis.Value(), 42);
    ASSERT_TRUE(settings->FindPair('efgh', &found_axis));
    ASSERT_EQ(found_axis.TagString(), AtomicString("efgh"));
    ASSERT_EQ(found_axis.Value(), 8118);
  }
}

TEST(FontSettingsTest, FindTestEmpty) {
  std::shared_ptr<FontVariationSettings> settings =
      MakeSettings<FontVariationSettings, FontVariationAxis>({});
  FontVariationAxis found_axis(0, 0);
  ASSERT_FALSE(settings->FindPair('aaaa', &found_axis));
}

} // namespace bkfont
