// Ported from: skia/include/core/SkMatrix.h, skia/src/utils/SkMatrix22.h

#pragma once

#include "rect.h"

namespace bkfont {

// SkMatrix restricted to its linear part. Every matrix on the glyph metrics
// path has zero translation and no perspective.
class ScalarMatrix {
public:
  constexpr ScalarMatrix() = default;

  static ScalarMatrix MakeAll(float scale_x, float skew_x, float skew_y, float scale_y) {
    ScalarMatrix m;
    m.scale_x_ = scale_x;
    m.skew_x_ = skew_x;
    m.skew_y_ = skew_y;
    m.scale_y_ = scale_y;
    return m;
  }
  static ScalarMatrix Scale(float sx, float sy) {
    ScalarMatrix m;
    m.SetScale(sx, sy);
    return m;
  }

  float GetScaleX() const {
    return scale_x_;
  }
  float GetSkewX() const {
    return skew_x_;
  }
  float GetSkewY() const {
    return skew_y_;
  }
  float GetScaleY() const {
    return scale_y_;
  }

  bool IsIdentity() const {
    return scale_x_ == 1 && skew_x_ == 0 && skew_y_ == 0 && scale_y_ == 1;
  }
  bool IsFinite() const;

  void Reset() {
    *this = ScalarMatrix();
  }
  void SetScale(float sx, float sy);
  void SetSkew(float sx, float sy);
  void SetSinCos(float sin_value, float cos_value);
  void SetConcat(const ScalarMatrix& a, const ScalarMatrix& b);

  ScalarMatrix& PreScale(float sx, float sy);
  ScalarMatrix& PreConcat(const ScalarMatrix& other);
  ScalarMatrix& PostConcat(const ScalarMatrix& other);
  ScalarMatrix& PostSkew(float sx, float sy);

  ScalarPoint MapPoint(ScalarPoint point) const;
  void MapRect(ScalarRect* rect) const;

private:
  bool IsScaleOnly() const {
    return skew_x_ == 0 && skew_y_ == 0;
  }

  float scale_x_ = 1;
  float skew_x_ = 0;
  float skew_y_ = 0;
  float scale_y_ = 1;
};

// SkComputeGivensRotation: finds G such that GA[0][1] is 0 for the vector h
// where A maps the horizontal baseline.
void ComputeGivensRotation(const ScalarPoint& h, ScalarMatrix* g);

} // namespace bkfont
