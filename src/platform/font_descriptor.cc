// Ported from: skia/src/core/SkFontDescriptor.cpp

#include "font_descriptor.h"

#include "paint/scalar.h"

namespace bkit {

namespace {

constexpr float kUsWidths[9]{
    1, 2, 3, 4, 5, 6, 7, 8, 9};

constexpr float kWidthForUsWidth[0x10] = {
    50,
    50, 62.5, 75, 87.5, 100, 112.5, 125, 150, 200,
    200, 200, 200, 200, 200, 200};

// SkScalarInterp.
float ScalarInterp(float a, float b, float t) {
  return a + (b - a) * t;
}

// SkScalarInterpFunc.
float ScalarInterpFunc(float search_key, const float keys[], const float values[], int length) {
  int right = 0;
  while (right < length && keys[right] < search_key) {
    ++right;
  }
  // Could use sentinel values to eliminate conditionals, but since the
  // tables are taken as input, a simpler format is better.
  if (right == length) {
    return values[length - 1];
  }
  if (right == 0) {
    return values[0];
  }
  // Otherwise, interpolate between right - 1 and right.
  float left_key = keys[right - 1];
  float right_key = keys[right];
  float fract = (search_key - left_key) / (right_key - left_key);
  return ScalarInterp(values[right - 1], values[right], fract);
}

} // namespace

FontStyle::Width FontStyleWidthForWidthAxisValue(float width) {
  int us_width = FloatRoundToInt(ScalarInterpFunc(width, &kWidthForUsWidth[1], kUsWidths, 9));
  return static_cast<FontStyle::Width>(us_width);
}

} // namespace bkit
