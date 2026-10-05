// Ported from: skia/src/core/SkGeometry.h
// Ported from: skia/src/core/SkGeometry.cpp
// Ported from: skia/src/core/SkPoint.cpp
// Ported from: skia/src/core/SkPointPriv.h

#pragma once

#include <cmath>

#include "matrix.h"
#include "rect.h"
#include "scalar.h"

namespace bkfont {

// SkPointPriv / SkPoint helpers on vectors.
namespace point {

inline bool CanNormalize(float dx, float dy) {
  return std::isfinite(dx) && std::isfinite(dy) && (dx || dy);
}
inline bool EqualsWithinTolerance(ScalarPoint p1, ScalarPoint p2) {
  return !CanNormalize(p1.x - p2.x, p1.y - p2.y);
}
inline bool ScalarNearlyZero(float x, float tolerance = kScalarNearlyZero) {
  return std::fabs(x) <= tolerance;
}
inline bool EqualsWithinTolerance(ScalarPoint pt, ScalarPoint p, float tol) {
  return ScalarNearlyZero(pt.x - p.x, tol) && ScalarNearlyZero(pt.y - p.y, tol);
}
inline float LengthSqd(ScalarPoint pt) {
  return pt.Dot(pt);
}
inline float DistanceToSqd(ScalarPoint pt, ScalarPoint a) {
  const float dx = pt.x - a.x;
  const float dy = pt.y - a.y;
  return dx * dx + dy * dy;
}
// SkPoint::Length.
float Length(float dx, float dy);
inline float Distance(ScalarPoint a, ScalarPoint b) {
  return Length(a.x - b.x, a.y - b.y);
}
inline void RotateCCW(ScalarPoint src, ScalarPoint* dst) {
  const float tmp = src.x;
  dst->x = src.y;
  dst->y = -tmp;
}
inline void RotateCW(ScalarPoint src, ScalarPoint* dst) {
  const float tmp = src.x;
  dst->x = -src.y;
  dst->y = tmp;
}
// SkPoint::setLength(x, y, length), computed in doubles.
bool SetLength(ScalarPoint* pt, float x, float y, float length);
inline bool SetLength(ScalarPoint* pt, float length) {
  return SetLength(pt, pt->x, pt->y, length);
}
inline bool SetNormalize(ScalarPoint* pt, float x, float y) {
  return SetLength(pt, x, y, 1);
}
inline bool Normalize(ScalarPoint* pt) {
  return SetLength(pt, pt->x, pt->y, 1);
}

} // namespace point

// SkRotationDirection.
enum class RotationDirection {
  kCW,
  kCCW,
};

// Returns 0 for 1 quad, and 1 for 2 quads, etc.
int FindUnitQuadRoots(float a, float b, float c, float roots[2]);

void EvalQuadAt(const ScalarPoint src[3], float t, ScalarPoint* pt, ScalarPoint* tangent = nullptr);
ScalarPoint EvalQuadAt(const ScalarPoint src[3], float t);
ScalarPoint EvalQuadTangentAt(const ScalarPoint src[3], float t);
void ChopQuadAt(const ScalarPoint src[3], ScalarPoint dst[5], float t);
void ChopQuadAtHalf(const ScalarPoint src[3], ScalarPoint dst[5]);
float FindQuadMaxCurvature(const ScalarPoint src[3]);

void EvalCubicAt(const ScalarPoint src[4], float t, ScalarPoint* loc, ScalarPoint* tangent, ScalarPoint* curvature);
void ChopCubicAt(const ScalarPoint src[4], ScalarPoint dst[7], float t);
void ChopCubicAtHalf(const ScalarPoint src[4], ScalarPoint dst[7]);
int FindCubicInflections(const ScalarPoint src[4], float t_values[2]);
int FindCubicMaxCurvature(const ScalarPoint src[4], float t_values[3]);
float FindCubicCusp(const ScalarPoint src[4]);

// SkConic.
struct Conic {
  static constexpr int kMaxConicsForArc = 5;
  static constexpr int kMaxConicToQuadPOW2 = 5;

  Conic() = default;
  Conic(ScalarPoint p0, ScalarPoint p1, ScalarPoint p2, float w) {
    Set(p0, p1, p2, w);
  }
  Conic(const ScalarPoint pts[3], float w) {
    Set(pts[0], pts[1], pts[2], w);
  }

  void Set(ScalarPoint p0, ScalarPoint p1, ScalarPoint p2, float w) {
    pts[0] = p0;
    pts[1] = p1;
    pts[2] = p2;
    SetW(w);
  }
  void Set(const ScalarPoint p[3], float w) {
    Set(p[0], p[1], p[2], w);
  }
  void SetW(float w) {
    // Guard against bad weights by forcing them to 1 (default).
    this->w = w > 0 && std::isfinite(w) ? w : 1;
  }

  ScalarPoint EvalAt(float t) const;
  ScalarPoint EvalTangentAt(float t) const;
  void EvalAt(float t, ScalarPoint* pt, ScalarPoint* tangent) const;
  // Returns false if infinity or NaN is generated; caller must check.
  bool ChopAt(float t, Conic dst[2]) const;
  void ChopAt(float t1, float t2, Conic* dst) const;
  void Chop(Conic dst[2]) const;
  int ComputeQuadPOW2(float tol) const;
  // Chop this conic into N quads, stored continguously in pts[], where
  // N = 1 << pow2. The amount of storage needed is (1 + 2 * N).
  int ChopIntoQuadsPOW2(ScalarPoint pts[], int pow2) const;

  // Set the conics for a unit arc from `start` to `stop`, transformed by
  // `user_matrix`; returns the number of conics.
  static int BuildUnitArc(ScalarPoint start, ScalarPoint stop, RotationDirection, const ScalarMatrix* user_matrix,
                          Conic dst[kMaxConicsForArc]);

  ScalarPoint pts[3];
  float w = 1;
};

} // namespace bkfont
