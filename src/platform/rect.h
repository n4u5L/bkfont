// Ported from: skia/include/core/SkPoint.h, skia/include/core/SkRect.h, skia/src/core/SkRect.cpp

#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>

namespace bkfont {

// SkPoint.
struct ScalarPoint {
  float x = 0;
  float y = 0;
};

// SkRect.
struct ScalarRect {
  float left = 0;
  float top = 0;
  float right = 0;
  float bottom = 0;

  static ScalarRect MakeLTRB(float l, float t, float r, float b) {
    return {l, t, r, b};
  }
  static ScalarRect MakeXYWH(float x, float y, float w, float h) {
    return {x, y, x + w, y + h};
  }

  float Width() const {
    return right - left;
  }
  float Height() const {
    return bottom - top;
  }

  // Written as the NOT of a non-empty rect, so NaN values are empty.
  bool IsEmpty() const {
    return !(left < right && top < bottom);
  }

  void Join(const ScalarRect& r) {
    if (r.IsEmpty()) return;

    if (IsEmpty()) {
      *this = r;
    } else {
      left = std::min(left, r.left);
      top = std::min(top, r.top);
      right = std::max(right, r.right);
      bottom = std::max(bottom, r.bottom);
    }
  }

  // roundOut(SkRect*) applied in place.
  void RoundOut() {
    *this = {std::floor(left), std::floor(top), std::ceil(right), std::ceil(bottom)};
  }

  // setBoundsNoCheck. Non-finite points produce a NaN rect.
  void SetBoundsNoCheck(std::span<const ScalarPoint> points) {
    float accum = 0;
    float min_x = points[0].x;
    float min_y = points[0].y;
    float max_x = points[0].x;
    float max_y = points[0].y;
    for (const ScalarPoint& point : points) {
      accum = accum * point.x * point.y;
      min_x = std::min(min_x, point.x);
      min_y = std::min(min_y, point.y);
      max_x = std::max(max_x, point.x);
      max_y = std::max(max_y, point.y);
    }
    if (accum * 0 == 0) {
      *this = {min_x, min_y, max_x, max_y};
    } else {
      const float nan = std::numeric_limits<float>::quiet_NaN();
      *this = {nan, nan, nan, nan};
    }
  }
};

} // namespace bkfont
