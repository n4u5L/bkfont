// Ported from: skia/include/core/SkPoint.h
// Ported from: skia/include/core/SkRect.h
// Ported from: skia/src/core/SkRect.cpp

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

namespace bkit {

// SkPoint.
struct ScalarPoint {
  float x = 0;
  float y = 0;

  bool IsZero() const {
    return (0 == x) & (0 == y);
  }
  // SkPoint::isFinite.
  bool IsFinite() const {
    float accum = 0;
    accum *= x;
    accum *= y;
    return accum == 0;
  }
  float Dot(ScalarPoint v) const {
    return x * v.x + y * v.y;
  }
  float Cross(ScalarPoint v) const {
    return x * v.y - y * v.x;
  }

  friend bool operator==(ScalarPoint a, ScalarPoint b) {
    return a.x == b.x && a.y == b.y;
  }
  friend ScalarPoint operator+(ScalarPoint a, ScalarPoint b) {
    return {a.x + b.x, a.y + b.y};
  }
  friend ScalarPoint operator-(ScalarPoint a, ScalarPoint b) {
    return {a.x - b.x, a.y - b.y};
  }
  friend ScalarPoint operator-(ScalarPoint a) {
    return {-a.x, -a.y};
  }
  friend ScalarPoint operator*(ScalarPoint a, float scale) {
    return {a.x * scale, a.y * scale};
  }
  ScalarPoint& operator+=(ScalarPoint v) {
    x += v.x;
    y += v.y;
    return *this;
  }
  ScalarPoint& operator-=(ScalarPoint v) {
    x -= v.x;
    y -= v.y;
    return *this;
  }
  ScalarPoint& operator*=(float scale) {
    x *= scale;
    y *= scale;
    return *this;
  }
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

  // SkRect::set(p0, p1): the sorted bounds of two points.
  static ScalarRect MakeBounds(ScalarPoint p0, ScalarPoint p1) {
    return {std::min(p0.x, p1.x), std::min(p0.y, p1.y), std::max(p0.x, p1.x), std::max(p0.y, p1.y)};
  }

  float Width() const {
    return right - left;
  }
  float Height() const {
    return bottom - top;
  }
  float CenterX() const {
    return left * 0.5f + right * 0.5f;
  }
  float CenterY() const {
    return top * 0.5f + bottom * 0.5f;
  }

  bool IsFinite() const {
    float accum = 0;
    accum *= left;
    accum *= top;
    accum *= right;
    accum *= bottom;
    return accum == 0;
  }

  void Sort() {
    if (left > right) std::swap(left, right);
    if (top > bottom) std::swap(top, bottom);
  }

  void Inset(float dx, float dy) {
    left += dx;
    top += dy;
    right -= dx;
    bottom -= dy;
  }

  // SkRect::intersect: false (and unchanged) when the rects do not intersect.
  bool Intersect(const ScalarRect& r) {
    const float l = std::max(left, r.left);
    const float t = std::max(top, r.top);
    const float rr = std::min(right, r.right);
    const float b = std::min(bottom, r.bottom);
    if (!(l < rr && t < b)) return false;
    *this = {l, t, rr, b};
    return true;
  }

  // SkRect::contains(const SkRect&).
  bool Contains(const ScalarRect& r) const {
    return !r.IsEmpty() && !IsEmpty() && left <= r.left && top <= r.top && right >= r.right && bottom >= r.bottom;
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

  void Offset(float dx, float dy) {
    left += dx;
    top += dy;
    right += dx;
    bottom += dy;
  }

  void Outset(float dx, float dy) {
    left -= dx;
    top -= dy;
    right += dx;
    bottom += dy;
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

// SkIRect.
struct IntRect {
  std::int32_t left = 0;
  std::int32_t top = 0;
  std::int32_t right = 0;
  std::int32_t bottom = 0;

  static IntRect MakeLTRB(std::int32_t l, std::int32_t t, std::int32_t r, std::int32_t b) {
    return {l, t, r, b};
  }
  static IntRect MakeWH(std::int32_t w, std::int32_t h) {
    return {0, 0, w, h};
  }
  // The right and bottom edges saturate, as Sk32_sat_add.
  static IntRect MakeXYWH(std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h) {
    return {x, y, SaturateAdd(x, w), SaturateAdd(y, h)};
  }

  // The span as an int32, which may overflow.
  std::int32_t Width() const {
    return static_cast<std::int32_t>(static_cast<std::int64_t>(right) - left);
  }
  std::int32_t Height() const {
    return static_cast<std::int32_t>(static_cast<std::int64_t>(bottom) - top);
  }
  std::int64_t Width64() const {
    return static_cast<std::int64_t>(right) - left;
  }
  std::int64_t Height64() const {
    return static_cast<std::int64_t>(bottom) - top;
  }

  // Empty if either span is not positive, computed in 64 bits.
  bool IsEmpty() const {
    return Width64() <= 0 || Height64() <= 0;
  }

  void Offset(std::int32_t dx, std::int32_t dy) {
    left = SaturateAdd(left, dx);
    top = SaturateAdd(top, dy);
    right = SaturateAdd(right, dx);
    bottom = SaturateAdd(bottom, dy);
  }

  bool operator==(const IntRect& other) const = default;

  // Both rects are assumed sorted.
  static bool Intersects(const IntRect& a, const IntRect& b) {
    const std::int32_t l = std::max(a.left, b.left);
    const std::int32_t r = std::min(a.right, b.right);
    const std::int32_t t = std::max(a.top, b.top);
    const std::int32_t bo = std::min(a.bottom, b.bottom);
    return l < r && t < bo;
  }

private:
  static std::int32_t SaturateAdd(std::int32_t a, std::int32_t b) {
    const std::int64_t sum = static_cast<std::int64_t>(a) + b;
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(sum, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()));
  }
};

} // namespace bkit
