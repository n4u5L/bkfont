// Official Chromium test: third_party/blink/renderer/platform/fonts/shaping/ng_shape_cache_test.cc
// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shaping/ng_shape_cache.h"

// Port: explicit shared ownership replaces the GC test environment.
#include "gtest/gtest.h"
#include "font/font.h"
#include "support/font_test_base.h"
#include "support/font_test_helpers.h"
#include "text/text_direction.h"

namespace bkfont {

class NGShapeCacheTest : public FontTestBase {
protected:
  void SetUp() override {
    font = test::CreateAhemFont(100);
    cache = std::make_shared<NGShapeCache>(font->PrimaryFont());
  }
  std::shared_ptr<Font> font;
  std::shared_ptr<NGShapeCache> cache;
};

TEST_F(NGShapeCacheTest, AddEntriesAndCacheHits) {
  auto ShapeResultFunc = []() -> std::shared_ptr<const ShapeResult> {
    // For the purposes of this test the actual internals of the shape result
    // doesn't matter.
    return std::make_shared<ShapeResult>(0, 0, TextDirection::kLtr);
  };

  // Adding an entry is successful.
  const auto entry_A_LTR =
      cache->GetOrCreate("A", TextDirection::kLtr, ShapeResultFunc);
  ASSERT_TRUE(entry_A_LTR);

  // Adding the same entry again hits cache.
  EXPECT_EQ(cache->GetOrCreate("A", TextDirection::kLtr, ShapeResultFunc),
            entry_A_LTR);

  // Adding the an entry with different text does not hit cache.
  const auto entry_B_LTR =
      cache->GetOrCreate("B", TextDirection::kLtr, ShapeResultFunc);
  ASSERT_TRUE(entry_B_LTR);
  EXPECT_NE(entry_B_LTR, entry_A_LTR);

  // Adding the same entry again hits cache.
  EXPECT_EQ(cache->GetOrCreate("B", TextDirection::kLtr, ShapeResultFunc),
            entry_B_LTR);

  // Adding the an entry with different direction does not hit cache.
  const auto entry_A_RTL =
      cache->GetOrCreate("A", TextDirection::kRtl, ShapeResultFunc);
  ASSERT_TRUE(entry_A_RTL);
  EXPECT_NE(entry_A_RTL, entry_A_LTR);
  EXPECT_NE(entry_A_RTL, entry_B_LTR);

  // Adding the same entry again hits cache.
  EXPECT_EQ(cache->GetOrCreate("A", TextDirection::kRtl, ShapeResultFunc),
            entry_A_RTL);
}

} // namespace bkfont
