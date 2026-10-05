// Ported from: skia/include/core/SkMatrix.h
// Ported from: skia/src/utils/SkMatrix22.h

#pragma once

#include "rect.h"

namespace bkfont {

// SkMatrix restricted to affine transforms. COLRv1 paints also use translation
// and transforms around a center; no glyph metrics transform uses perspective.
class ScalarMatrix {
public:
  constexpr ScalarMatrix() = default;

  static ScalarMatrix MakeAll(float scale_x, float skew_x, float skew_y, float scale_y) {
    return MakeAll(scale_x, skew_x, 0, skew_y, scale_y, 0);
  }
  static ScalarMatrix MakeAll(float scale_x, float skew_x, float translate_x,
                              float skew_y, float scale_y, float translate_y) {
    ScalarMatrix m;
    m.scale_x_ = scale_x;
    m.skew_x_ = skew_x;
    m.skew_y_ = skew_y;
    m.scale_y_ = scale_y;
    m.translate_x_ = translate_x;
    m.translate_y_ = translate_y;
    return m;
  }
  static ScalarMatrix Scale(float sx, float sy) {
    ScalarMatrix m;
    m.SetScale(sx, sy);
    return m;
  }
  static ScalarMatrix Translate(float dx, float dy) {
    return MakeAll(1, 0, dx, 0, 1, dy);
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
  float GetTranslateX() const {
    return translate_x_;
  }
  float GetTranslateY() const {
    return translate_y_;
  }

  // SkMatrix::TypeMask without the perspective bit.
  enum TypeMask : unsigned {
    kIdentity_Mask = 0,
    kTranslate_Mask = 0x01,
    kScale_Mask = 0x02,
    kAffine_Mask = 0x04,
  };

  // SkMatrix::getType. Skew sets the scale bit along with the affine bit.
  unsigned GetType() const;

  bool IsIdentity() const {
    return scale_x_ == 1 && skew_x_ == 0 && skew_y_ == 0 && scale_y_ == 1 && translate_x_ == 0 && translate_y_ == 0;
  }
  bool IsFinite() const;

  void Reset() {
    *this = ScalarMatrix();
  }
  void SetScale(float sx, float sy);
  void SetScale(float sx, float sy, float px, float py);
  void SetSkew(float sx, float sy);
  void SetSkew(float sx, float sy, float px, float py);
  void SetSinCos(float sin_value, float cos_value);
  void SetSinCos(float sin_value, float cos_value, float px, float py);
  void SetRotate(float degrees, float px = 0, float py = 0);
  void SetConcat(const ScalarMatrix& a, const ScalarMatrix& b);

  ScalarMatrix& PreScale(float sx, float sy);
  ScalarMatrix& PreTranslate(float dx, float dy);
  ScalarMatrix& PreConcat(const ScalarMatrix& other);
  ScalarMatrix& PostConcat(const ScalarMatrix& other);
  ScalarMatrix& PostSkew(float sx, float sy);
  ScalarMatrix& PostScale(float sx, float sy);
  ScalarMatrix& PostTranslate(float dx, float dy);

  // SkMatrix::invert. Returns false if the matrix is not invertible, leaving
  // inverse untouched.
  bool Invert(ScalarMatrix* inverse) const;

  ScalarPoint MapPoint(ScalarPoint point) const;
  void MapRect(ScalarRect* rect) const;

private:
  bool IsScaleTranslate() const {
    return skew_x_ == 0 && skew_y_ == 0;
  }

  float scale_x_ = 1;
  float skew_x_ = 0;
  float skew_y_ = 0;
  float scale_y_ = 1;
  float translate_x_ = 0;
  float translate_y_ = 0;
};

// SkComputeGivensRotation: finds G such that GA[0][1] is 0 for the vector h
// where A maps the horizontal baseline.
void ComputeGivensRotation(const ScalarPoint& h, ScalarMatrix* g);

} // namespace bkfont
