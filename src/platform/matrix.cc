// Ported from: skia/src/core/SkMatrix.cpp, skia/src/utils/SkMatrix22.cpp

#include "matrix.h"

#include <array>
#include <cmath>

namespace bkfont {

namespace {

float MulAddMul(float a, float b, float c, float d) {
  return static_cast<float>(static_cast<double>(a) * b + static_cast<double>(c) * d);
}

} // namespace

bool ScalarMatrix::IsFinite() const {
  return std::isfinite(scale_x_) && std::isfinite(skew_x_) && std::isfinite(skew_y_) && std::isfinite(scale_y_);
}

void ScalarMatrix::SetScale(float sx, float sy) {
  if (1 == sx && 1 == sy) {
    Reset();
  } else {
    scale_x_ = sx;
    skew_x_ = 0;
    skew_y_ = 0;
    scale_y_ = sy;
  }
}

void ScalarMatrix::SetSkew(float sx, float sy) {
  scale_x_ = 1;
  skew_x_ = sx;
  skew_y_ = sy;
  scale_y_ = 1;
}

void ScalarMatrix::SetSinCos(float sin_value, float cos_value) {
  scale_x_ = cos_value;
  skew_x_ = -sin_value;
  skew_y_ = sin_value;
  scale_y_ = cos_value;
}

void ScalarMatrix::SetConcat(const ScalarMatrix& a, const ScalarMatrix& b) {
  if (a.IsIdentity()) {
    *this = b;
  } else if (b.IsIdentity()) {
    *this = a;
  } else if (a.IsScaleOnly() && b.IsScaleOnly()) {
    const float sx = a.scale_x_ * b.scale_x_;
    const float sy = a.scale_y_ * b.scale_y_;
    scale_x_ = sx;
    skew_x_ = 0;
    skew_y_ = 0;
    scale_y_ = sy;
  } else {
    ScalarMatrix tmp;
    tmp.scale_x_ = MulAddMul(a.scale_x_, b.scale_x_, a.skew_x_, b.skew_y_);
    tmp.skew_x_ = MulAddMul(a.scale_x_, b.skew_x_, a.skew_x_, b.scale_y_);
    tmp.skew_y_ = MulAddMul(a.skew_y_, b.scale_x_, a.scale_y_, b.skew_y_);
    tmp.scale_y_ = MulAddMul(a.skew_y_, b.skew_x_, a.scale_y_, b.scale_y_);
    *this = tmp;
  }
}

ScalarMatrix& ScalarMatrix::PreScale(float sx, float sy) {
  if (1 == sx && 1 == sy) return *this;

  scale_x_ *= sx;
  skew_y_ *= sx;
  skew_x_ *= sy;
  scale_y_ *= sy;
  return *this;
}

ScalarMatrix& ScalarMatrix::PreConcat(const ScalarMatrix& other) {
  // Check for identity first, so we don't do a needless copy of ourselves to
  // ourselves inside SetConcat().
  if (!other.IsIdentity()) SetConcat(*this, other);
  return *this;
}

ScalarMatrix& ScalarMatrix::PostConcat(const ScalarMatrix& other) {
  if (!other.IsIdentity()) SetConcat(other, *this);
  return *this;
}

ScalarMatrix& ScalarMatrix::PostSkew(float sx, float sy) {
  ScalarMatrix m;
  m.SetSkew(sx, sy);
  return PostConcat(m);
}

ScalarPoint ScalarMatrix::MapPoint(ScalarPoint point) const {
  if (IsScaleOnly()) return {point.x * scale_x_, point.y * scale_y_};
  return {point.x * scale_x_ + point.y * skew_x_, point.y * scale_y_ + point.x * skew_y_};
}

void ScalarMatrix::MapRect(ScalarRect* rect) const {
  if (IsScaleOnly()) {
    // mapRectScaleTranslate followed by sort_as_rect.
    const float l = rect->left * scale_x_;
    const float t = rect->top * scale_y_;
    const float r = rect->right * scale_x_;
    const float b = rect->bottom * scale_y_;
    *rect = {std::min(l, r), std::min(t, b), std::max(l, r), std::max(t, b)};
    return;
  }
  const std::array<ScalarPoint, 4> quad = {MapPoint({rect->left, rect->top}), MapPoint({rect->right, rect->top}), MapPoint({rect->right, rect->bottom}), MapPoint({rect->left, rect->bottom})};
  rect->SetBoundsNoCheck(quad);
}

void ComputeGivensRotation(const ScalarPoint& h, ScalarMatrix* g) {
  const float& a = h.x;
  const float& b = h.y;
  float c;
  float s;
  if (0 == b) {
    c = std::copysign(1.0f, a);
    s = 0;
  } else if (0 == a) {
    c = 0;
    s = -std::copysign(1.0f, b);
  } else if (std::abs(b) > std::abs(a)) {
    const float t = a / b;
    const float u = std::copysign(std::sqrt(1.0f + t * t), b);
    s = -1.0f / u;
    c = -s * t;
  } else {
    const float t = b / a;
    const float u = std::copysign(std::sqrt(1.0f + t * t), a);
    c = 1.0f / u;
    s = -c * t;
  }

  g->SetSinCos(s, c);
}

} // namespace bkfont
