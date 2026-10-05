// Ported from: skia/src/core/SkMatrix.cpp
// Ported from: skia/src/utils/SkMatrix22.cpp

#include "matrix.h"

#include <array>
#include <cmath>

#include "scalar.h"

namespace bkfont {

namespace {

float MulAddMul(float a, float b, float c, float d) {
  return static_cast<float>(static_cast<double>(a) * b + static_cast<double>(c) * d);
}

} // namespace

unsigned ScalarMatrix::GetType() const {
  unsigned mask = kIdentity_Mask;
  if (translate_x_ != 0 || translate_y_ != 0) {
    mask |= kTranslate_Mask;
  }
  // The skew components may be scale-inducing, unless we are dealing with a
  // pure rotation, so the scale bit is always set along with affine.
  if (skew_x_ != 0 || skew_y_ != 0) {
    mask |= kAffine_Mask | kScale_Mask;
  } else if (scale_x_ != 1 || scale_y_ != 1) {
    mask |= kScale_Mask;
  }
  return mask;
}

bool ScalarMatrix::IsFinite() const {
  return std::isfinite(scale_x_) && std::isfinite(skew_x_) && std::isfinite(skew_y_) && std::isfinite(scale_y_) && std::isfinite(translate_x_) && std::isfinite(translate_y_);
}

void ScalarMatrix::SetScale(float sx, float sy) {
  if (1 == sx && 1 == sy) {
    Reset();
  } else {
    scale_x_ = sx;
    skew_x_ = 0;
    skew_y_ = 0;
    scale_y_ = sy;
    translate_x_ = 0;
    translate_y_ = 0;
  }
}

void ScalarMatrix::SetScale(float sx, float sy, float px, float py) {
  SetScale(sx, sy);
  if (sx != 1 || sy != 1) {
    translate_x_ = px - sx * px;
    translate_y_ = py - sy * py;
  }
}

void ScalarMatrix::SetSkew(float sx, float sy) {
  scale_x_ = 1;
  skew_x_ = sx;
  skew_y_ = sy;
  scale_y_ = 1;
  translate_x_ = 0;
  translate_y_ = 0;
}

void ScalarMatrix::SetSkew(float sx, float sy, float px, float py) {
  SetSkew(sx, sy);
  translate_x_ = -sx * py;
  translate_y_ = -sy * px;
}

void ScalarMatrix::SetSinCos(float sin_value, float cos_value) {
  scale_x_ = cos_value;
  skew_x_ = -sin_value;
  skew_y_ = sin_value;
  scale_y_ = cos_value;
  translate_x_ = 0;
  translate_y_ = 0;
}

void ScalarMatrix::SetSinCos(float sin_value, float cos_value, float px, float py) {
  SetSinCos(sin_value, cos_value);
  const float one_minus_cos = 1 - cos_value;
  translate_x_ = sin_value * py + one_minus_cos * px;
  translate_y_ = -sin_value * px + one_minus_cos * py;
}

void ScalarMatrix::SetRotate(float degrees, float px, float py) {
  const float radians = degrees * (3.14159265358979323846f / 180);
  float sin_value = std::sin(radians);
  float cos_value = std::cos(radians);
  if (std::abs(sin_value) <= kScalarSinCosNearlyZero) sin_value = 0;
  if (std::abs(cos_value) <= kScalarSinCosNearlyZero) cos_value = 0;
  SetSinCos(sin_value, cos_value, px, py);
}

void ScalarMatrix::SetConcat(const ScalarMatrix& a, const ScalarMatrix& b) {
  if (a.IsIdentity()) {
    *this = b;
  } else if (b.IsIdentity()) {
    *this = a;
  } else if (a.IsScaleTranslate() && b.IsScaleTranslate()) {
    const float sx = a.scale_x_ * b.scale_x_;
    const float sy = a.scale_y_ * b.scale_y_;
    const float tx = a.scale_x_ * b.translate_x_ + a.translate_x_;
    const float ty = a.scale_y_ * b.translate_y_ + a.translate_y_;
    scale_x_ = sx;
    skew_x_ = 0;
    skew_y_ = 0;
    scale_y_ = sy;
    translate_x_ = tx;
    translate_y_ = ty;
  } else {
    ScalarMatrix tmp;
    tmp.scale_x_ = MulAddMul(a.scale_x_, b.scale_x_, a.skew_x_, b.skew_y_);
    tmp.skew_x_ = MulAddMul(a.scale_x_, b.skew_x_, a.skew_x_, b.scale_y_);
    tmp.skew_y_ = MulAddMul(a.skew_y_, b.scale_x_, a.scale_y_, b.skew_y_);
    tmp.scale_y_ = MulAddMul(a.skew_y_, b.skew_x_, a.scale_y_, b.scale_y_);
    tmp.translate_x_ = MulAddMul(a.scale_x_, b.translate_x_, a.skew_x_, b.translate_y_) + a.translate_x_;
    tmp.translate_y_ = MulAddMul(a.skew_y_, b.translate_x_, a.scale_y_, b.translate_y_) + a.translate_y_;
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

ScalarMatrix& ScalarMatrix::PreTranslate(float dx, float dy) {
  if (dx == 0 && dy == 0) return *this;

  if (IsScaleTranslate()) {
    translate_x_ += dx * scale_x_;
    translate_y_ += dy * scale_y_;
  } else {
    translate_x_ += MulAddMul(scale_x_, dx, skew_x_, dy);
    translate_y_ += MulAddMul(skew_y_, dx, scale_y_, dy);
  }
  return *this;
}

ScalarMatrix& ScalarMatrix::PostScale(float sx, float sy) {
  if (1 == sx && 1 == sy) return *this;
  return PostConcat(ScalarMatrix::Scale(sx, sy));
}

ScalarMatrix& ScalarMatrix::PostTranslate(float dx, float dy) {
  translate_x_ += dx;
  translate_y_ += dy;
  return *this;
}

bool ScalarMatrix::Invert(ScalarMatrix* inverse) const {
  if (IsIdentity()) {
    *inverse = ScalarMatrix();
    return true;
  }

  if (IsScaleTranslate()) {
    if (scale_x_ == 0 || scale_y_ == 0) {
      return false;
    }
    const float inv_x = 1 / scale_x_;
    const float inv_y = 1 / scale_y_;
    ScalarMatrix result = MakeAll(inv_x, 0, -translate_x_ * inv_x, 0, inv_y, -translate_y_ * inv_y);
    if (!result.IsFinite()) {
      return false;
    }
    *inverse = result;
    return true;
  }

  // sk_inv_determinant: the determinant is computed in double and rejected
  // when nearly zero.
  const double det = static_cast<double>(scale_x_) * scale_y_ - static_cast<double>(skew_x_) * skew_y_;
  constexpr double kNearlyZeroCubed = static_cast<double>(kScalarNearlyZero) * kScalarNearlyZero * kScalarNearlyZero;
  if (std::abs(det) <= kNearlyZeroCubed) {
    return false;
  }
  const double inv_det = 1.0 / det;

  ScalarMatrix result;
  result.scale_x_ = static_cast<float>(scale_y_ * inv_det);
  result.skew_x_ = static_cast<float>(-skew_x_ * inv_det);
  result.translate_x_ = static_cast<float>((static_cast<double>(skew_x_) * translate_y_ - static_cast<double>(scale_y_) * translate_x_) * inv_det);
  result.skew_y_ = static_cast<float>(-skew_y_ * inv_det);
  result.scale_y_ = static_cast<float>(scale_x_ * inv_det);
  result.translate_y_ = static_cast<float>((static_cast<double>(skew_y_) * translate_x_ - static_cast<double>(scale_x_) * translate_y_) * inv_det);
  if (!result.IsFinite()) {
    return false;
  }
  *inverse = result;
  return true;
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
  if (IsScaleTranslate()) return {point.x * scale_x_ + translate_x_, point.y * scale_y_ + translate_y_};
  return {point.x * scale_x_ + point.y * skew_x_ + translate_x_, point.y * scale_y_ + point.x * skew_y_ + translate_y_};
}

void ScalarMatrix::MapRect(ScalarRect* rect) const {
  if (IsScaleTranslate()) {
    // mapRectScaleTranslate followed by sort_as_rect.
    const float l = rect->left * scale_x_ + translate_x_;
    const float t = rect->top * scale_y_ + translate_y_;
    const float r = rect->right * scale_x_ + translate_x_;
    const float b = rect->bottom * scale_y_ + translate_y_;
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
