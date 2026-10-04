// Copyright 2012 The Chromium Authors
// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license.
// Font-only extraction from ui/gfx/geometry/{point_f,size_f,vector2d_f,rect_f}.{h,cc}.
#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace bkfont::gfx {
// Extracted from ui/gfx/geometry/{insets_outsets_f_base,insets_f,outsets_f}.h.
// This is the base template class of InsetsF and OutsetsF.
template <typename T>
class InsetsOutsetsFBase {
public:
  constexpr InsetsOutsetsFBase() = default;
  constexpr explicit InsetsOutsetsFBase(float all)
      : top_(all),
        left_(all),
        bottom_(all),
        right_(all) {
  }

  constexpr float top() const {
    return top_;
  }
  constexpr float left() const {
    return left_;
  }
  constexpr float bottom() const {
    return bottom_;
  }
  constexpr float right() const {
    return right_;
  }

  // Returns the total width taken up by the insets/outsets, which is the
  // sum of the left and right insets/outsets.
  constexpr float width() const {
    return left_ + right_;
  }

  // Returns the total height taken up by the insets/outsets, which is the
  // sum of the top and bottom insets/outsets.
  constexpr float height() const {
    return top_ + bottom_;
  }

  // Returns true if the insets/outsets are empty.
  bool IsEmpty() const {
    return width() == 0.f && height() == 0.f;
  }

  // These setters can be used together with the default constructor and the
  // single-parameter constructor to construct InsetsF instances, for example:
  //                                                    // T, L, B, R
  //   InsetsF a = InsetsF().set_top(2);                // 2, 0, 0, 0
  //   InsetsF b = InsetsF().set_left(2).set_bottom(3); // 0, 2, 3, 0
  //   InsetsF c = InsetsF(1).set_top(5);               // 5, 1, 1, 1
  constexpr T& set_top(float top) {
    top_ = top;
    return *static_cast<T*>(this);
  }
  constexpr T& set_left(float left) {
    left_ = left;
    return *static_cast<T*>(this);
  }
  constexpr T& set_bottom(float bottom) {
    bottom_ = bottom;
    return *static_cast<T*>(this);
  }
  constexpr T& set_right(float right) {
    right_ = right;
    return *static_cast<T*>(this);
  }

  // In addition to the above, we can also use the following methods to
  // construct InsetsF/OutsetsF.
  // TLBR() is for Chomium UI code. We should not use it in blink code because
  // the order of parameters is different from the normal orders used in blink.
  // Blink code can use the above setters and VH().
  static constexpr inline T TLBR(float top,
                                 float left,
                                 float bottom,
                                 float right) {
    return T().set_top(top).set_left(left).set_bottom(bottom).set_right(right);
  }
  static constexpr inline T VH(float vertical, float horizontal) {
    return TLBR(vertical, horizontal, vertical, horizontal);
  }

  // Sets each side to the maximum of the side and the corresponding side of
  // |other|.
  void SetToMax(const T& other) {
    top_ = std::max(top_, other.top_);
    left_ = std::max(left_, other.left_);
    bottom_ = std::max(bottom_, other.bottom_);
    right_ = std::max(right_, other.right_);
  }

  void Scale(float x_scale, float y_scale) {
    top_ *= y_scale;
    left_ *= x_scale;
    bottom_ *= y_scale;
    right_ *= x_scale;
  }
  void Scale(float scale) {
    Scale(scale, scale);
  }

  friend bool operator==(const InsetsOutsetsFBase<T>&,
                         const InsetsOutsetsFBase<T>&) = default;

  void operator+=(const T& other) {
    top_ += other.top_;
    left_ += other.left_;
    bottom_ += other.bottom_;
    right_ += other.right_;
  }

  void operator-=(const T& other) {
    top_ -= other.top_;
    left_ -= other.left_;
    bottom_ -= other.bottom_;
    right_ -= other.right_;
  }

  T operator-() const {
    return T().set_left(-left_).set_right(-right_).set_top(-top_).set_bottom(
        -bottom_);
  }

private:
  float top_ = 0.f;
  float left_ = 0.f;
  float bottom_ = 0.f;
  float right_ = 0.f;
};

class OutsetsF;

class InsetsF : public InsetsOutsetsFBase<InsetsF> {
public:
  using InsetsOutsetsFBase::InsetsOutsetsFBase;
  OutsetsF ToOutsets() const;
};

class OutsetsF : public InsetsOutsetsFBase<OutsetsF> {
public:
  using InsetsOutsetsFBase::InsetsOutsetsFBase;
  InsetsF ToInsets() const {
    return InsetsF()
        .set_left(-left())
        .set_right(-right())
        .set_top(-top())
        .set_bottom(-bottom());
  }
};

inline OutsetsF InsetsF::ToOutsets() const {
  return OutsetsF()
      .set_left(-left())
      .set_right(-right())
      .set_top(-top())
      .set_bottom(-bottom());
}

class Vector2dF {
public:
  constexpr Vector2dF() = default;
  constexpr Vector2dF(float x, float y)
      : x_(x),
        y_(y) {
  }
  constexpr float x() const {
    return x_;
  }
  constexpr float y() const {
    return y_;
  }
  void set_x(float value) {
    x_ = value;
  }
  void set_y(float value) {
    y_ = value;
  }
  bool IsZero() const {
    return x_ == 0 && y_ == 0;
  }
  void Add(const Vector2dF& other) {
    x_ += other.x_;
    y_ += other.y_;
  }
  void Subtract(const Vector2dF& other) {
    x_ -= other.x_;
    y_ -= other.y_;
  }
  void operator+=(const Vector2dF& other) {
    Add(other);
  }
  void operator-=(const Vector2dF& other) {
    Subtract(other);
  }
  void Scale(float scale) {
    Scale(scale, scale);
  }
  void Scale(float x_scale, float y_scale) {
    x_ *= x_scale;
    y_ *= y_scale;
  }
  void Transpose() {
    std::swap(x_, y_);
  }
  friend constexpr bool operator==(const Vector2dF&, const Vector2dF&) = default;

private:
  float x_ = 0;
  float y_ = 0;
};
inline constexpr Vector2dF operator-(const Vector2dF& value) {
  return {-value.x(), -value.y()};
}
inline Vector2dF operator+(Vector2dF lhs, const Vector2dF& rhs) {
  lhs += rhs;
  return lhs;
}
inline Vector2dF operator-(Vector2dF lhs, const Vector2dF& rhs) {
  lhs -= rhs;
  return lhs;
}

class PointF {
public:
  constexpr PointF() = default;
  constexpr PointF(float x, float y)
      : x_(x),
        y_(y) {
  }
  constexpr float x() const {
    return x_;
  }
  constexpr float y() const {
    return y_;
  }
  void set_x(float value) {
    x_ = value;
  }
  void set_y(float value) {
    y_ = value;
  }
  void SetPoint(float x, float y) {
    x_ = x;
    y_ = y;
  }
  void Offset(float x, float y) {
    x_ += x;
    y_ += y;
  }
  constexpr void operator+=(const Vector2dF& vector) {
    x_ += vector.x();
    y_ += vector.y();
  }
  constexpr void operator-=(const Vector2dF& vector) {
    x_ -= vector.x();
    y_ -= vector.y();
  }
  void Transpose() {
    std::swap(x_, y_);
  }
  constexpr Vector2dF OffsetFromOrigin() const {
    return {x_, y_};
  }
  friend constexpr bool operator==(const PointF&, const PointF&) = default;

private:
  float x_ = 0;
  float y_ = 0;
};
inline PointF operator+(PointF lhs, const Vector2dF& rhs) {
  lhs += rhs;
  return lhs;
}
inline PointF operator-(PointF lhs, const Vector2dF& rhs) {
  lhs -= rhs;
  return lhs;
}
inline Vector2dF operator-(const PointF& lhs, const PointF& rhs) {
  return {lhs.x() - rhs.x(), lhs.y() - rhs.y()};
}

class SizeF {
public:
  constexpr SizeF() = default;
  constexpr SizeF(float width, float height)
      : width_(clamp(width)),
        height_(clamp(height)) {
  }
  constexpr float width() const {
    return width_;
  }
  constexpr float height() const {
    return height_;
  }
  void set_width(float value) {
    width_ = clamp(value);
  }
  void set_height(float value) {
    height_ = clamp(value);
  }
  void SetSize(float width, float height) {
    width_ = clamp(width);
    height_ = clamp(height);
  }
  void SetToNextWidth() {
    width_ = next(width_);
  }
  void SetToNextHeight() {
    height_ = next(height_);
  }
  constexpr bool IsEmpty() const {
    return !width_ || !height_;
  }
  constexpr bool IsZero() const {
    return !width_ && !height_;
  }
  void Transpose() {
    std::swap(width_, height_);
  }
  friend constexpr bool operator==(const SizeF&, const SizeF&) = default;

private:
  static constexpr float kTrivial = 8.f * std::numeric_limits<float>::epsilon();
  static constexpr float clamp(float value) {
    return value > kTrivial ? value : 0.f;
  }
  static float next(float value) {
    return std::nextafter(std::max(kTrivial, value), std::numeric_limits<float>::max());
  }
  float width_ = 0;
  float height_ = 0;
};

class RectF {
public:
  constexpr RectF() = default;
  constexpr RectF(float x, float y, float width, float height)
      : origin_(x, y),
        size_(width, height) {
  }
  constexpr RectF(const PointF& origin, const SizeF& size)
      : origin_(origin),
        size_(size) {
  }
  constexpr float x() const {
    return origin_.x();
  }
  constexpr float y() const {
    return origin_.y();
  }
  constexpr float width() const {
    return size_.width();
  }
  constexpr float height() const {
    return size_.height();
  }
  constexpr float right() const {
    return x() + width();
  }
  constexpr float bottom() const {
    return y() + height();
  }
  const PointF& origin() const {
    return origin_;
  }
  const SizeF& size() const {
    return size_;
  }
  void set_x(float value) {
    origin_.set_x(value);
  }
  void set_y(float value) {
    origin_.set_y(value);
  }
  void set_width(float value) {
    size_.set_width(value);
  }
  void set_height(float value) {
    size_.set_height(value);
  }
  void set_origin(const PointF& origin) {
    origin_ = origin;
  }
  void set_size(const SizeF& size) {
    size_ = size;
  }
  void SetRect(float x, float y, float width, float height) {
    origin_.SetPoint(x, y);
    size_.SetSize(width, height);
  }
  bool IsEmpty() const {
    return size_.IsEmpty();
  }
  void Inset(const InsetsF& insets) {
    origin_ += Vector2dF(insets.left(), insets.top());
    set_width(width() - insets.width());
    set_height(height() - insets.height());
  }
  void Inset(float inset) {
    Inset(InsetsF(inset));
  }
  void Outset(const OutsetsF& outsets) {
    Inset(outsets.ToInsets());
  }
  void Outset(float outset) {
    Inset(-outset);
  }
  void Offset(float x, float y) {
    origin_ += Vector2dF(x, y);
  }
  void Offset(const Vector2dF& offset) {
    origin_ += offset;
  }
  void operator+=(const Vector2dF& offset) {
    Offset(offset);
  }
  void operator-=(const Vector2dF& offset) {
    origin_ -= offset;
  }
  void Transpose() {
    origin_.Transpose();
    size_.Transpose();
  }
  void Union(const RectF& rect) {
    if (IsEmpty()) {
      *this = rect;
      return;
    }
    if (rect.IsEmpty()) return;
    UnionEvenIfEmpty(rect);
  }
  void UnionEvenIfEmpty(const RectF& rect) {
    float rx = std::min(x(), rect.x());
    float ry = std::min(y(), rect.y());
    float rr = std::max(right(), rect.right());
    float rb = std::max(bottom(), rect.bottom());
    SetRect(rx, ry, rr - rx, rb - ry);
    constexpr auto kFloatMax = std::numeric_limits<float>::max();
    if (right() < rr && width() < kFloatMax) size_.SetToNextWidth();
    if (bottom() < rb && height() < kFloatMax) size_.SetToNextHeight();
  }
  friend bool operator==(const RectF&, const RectF&) = default;

private:
  PointF origin_;
  SizeF size_;
};
inline RectF operator+(RectF rect, const Vector2dF& offset) {
  rect += offset;
  return rect;
}
inline RectF operator-(RectF rect, const Vector2dF& offset) {
  rect -= offset;
  return rect;
}
} // namespace bkfont::gfx
