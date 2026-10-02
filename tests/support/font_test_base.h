// Source: Blink platform/testing/font_test_base.h/.cc.
// The standalone test main initializes fonts. DirectWrite does not need the
// upstream renderer TaskEnvironment or Skia's PNG-decoder registration.
#pragma once
#include "gtest/gtest.h"
namespace blink {
class FontTestBase : public ::testing::Test {};
} // namespace blink
