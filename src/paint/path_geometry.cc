// Ported from: skia/src/core/SkGeometry.cpp
// Ported from: skia/src/core/SkPoint.cpp

#include "path_geometry.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace bkfont {

namespace point {

float Length(float dx, float dy) {
  const float mag2 = dx * dx + dy * dy;
  if (std::isfinite(mag2)) return std::sqrt(mag2);
  const double xx = dx;
  const double yy = dy;
  return static_cast<float>(std::sqrt(xx * xx + yy * yy));
}

// set_point_length<false>: computed in doubles so x * x + y * y does not
// overflow.
bool SetLength(ScalarPoint* pt, float x, float y, float length) {
  const double xx = x;
  const double yy = y;
  const double dmag = std::sqrt(xx * xx + yy * yy);
  const double dscale = static_cast<double>(length) / dmag;
  x = static_cast<float>(x * dscale);
  y = static_cast<float>(y * dscale);
  // check if we're not finite, or we're zero-length
  if (!std::isfinite(x) || !std::isfinite(y) || (x == 0 && y == 0)) {
    *pt = {0, 0};
    return false;
  }
  *pt = {x, y};
  return true;
}

} // namespace point

namespace {

ScalarPoint Times2(ScalarPoint value) {
  return value + value;
}

int ValidUnitDivide(float numer, float denom, float* ratio) {
  if (numer < 0) {
    numer = -numer;
    denom = -denom;
  }
  if (denom == 0 || numer == 0 || numer >= denom) return 0;
  const float r = numer / denom;
  if (std::isnan(r)) return 0;
  if (r == 0) return 0; // catch underflow if numer <<<< denom
  *ratio = r;
  return 1;
}

// SkQuadCoeff.
struct QuadCoeff {
  QuadCoeff() = default;
  QuadCoeff(ScalarPoint a, ScalarPoint b, ScalarPoint c)
      : a(a),
        b(b),
        c(c) {
  }
  explicit QuadCoeff(const ScalarPoint src[3]) {
    c = src[0];
    const ScalarPoint p1 = src[1];
    const ScalarPoint p2 = src[2];
    b = Times2(p1 - c);
    a = p2 - Times2(p1) + c;
  }
  ScalarPoint Eval(float t) const {
    return {(a.x * t + b.x) * t + c.x, (a.y * t + b.y) * t + c.y};
  }
  ScalarPoint a;
  ScalarPoint b;
  ScalarPoint c;
};

// SkCubicCoeff.
struct CubicCoeff {
  explicit CubicCoeff(const ScalarPoint src[4]) {
    const ScalarPoint p0 = src[0];
    const ScalarPoint p1 = src[1];
    const ScalarPoint p2 = src[2];
    const ScalarPoint p3 = src[3];
    a = p3 + (p1 - p2) * 3 - p0;
    b = (p2 - Times2(p1) + p0) * 3;
    c = (p1 - p0) * 3;
    d = p0;
  }
  ScalarPoint Eval(float t) const {
    return {((a.x * t + b.x) * t + c.x) * t + d.x, ((a.y * t + b.y) * t + c.y) * t + d.y};
  }
  ScalarPoint a;
  ScalarPoint b;
  ScalarPoint c;
  ScalarPoint d;
};

// SkConicCoeff.
struct ConicCoeff {
  explicit ConicCoeff(const Conic& conic) {
    const ScalarPoint p0 = conic.pts[0];
    const ScalarPoint p1 = conic.pts[1];
    const ScalarPoint p2 = conic.pts[2];
    const float ww = conic.w;
    const ScalarPoint p1w = p1 * ww;
    numer.c = p0;
    numer.a = p2 - Times2(p1w) + p0;
    numer.b = Times2(p1w - p0);
    denom.c = {1, 1};
    denom.b = Times2(ScalarPoint{ww, ww} - denom.c);
    denom.a = ScalarPoint{0, 0} - denom.b;
  }
  ScalarPoint Eval(float t) const {
    const ScalarPoint n = numer.Eval(t);
    const ScalarPoint d = denom.Eval(t);
    return {n.x / d.x, n.y / d.y};
  }
  QuadCoeff numer;
  QuadCoeff denom;
};

ScalarPoint Interp(ScalarPoint v0, ScalarPoint v1, float t) {
  return v0 + (v1 - v0) * t;
}

// unchecked_mix.
ScalarPoint UncheckedMix(ScalarPoint a, ScalarPoint b, float t) {
  return (b - a) * t + a;
}

ScalarPoint EvalCubicDerivative(const ScalarPoint src[4], float t) {
  const ScalarPoint p0 = src[0];
  const ScalarPoint p1 = src[1];
  const ScalarPoint p2 = src[2];
  const ScalarPoint p3 = src[3];
  QuadCoeff coeff;
  coeff.a = p3 + (p1 - p2) * 3 - p0;
  coeff.b = Times2(p2 - Times2(p1) + p0);
  coeff.c = p1 - p0;
  return coeff.Eval(t);
}

ScalarPoint EvalCubic2ndDerivative(const ScalarPoint src[4], float t) {
  const ScalarPoint p0 = src[0];
  const ScalarPoint p1 = src[1];
  const ScalarPoint p2 = src[2];
  const ScalarPoint p3 = src[3];
  const ScalarPoint a = p3 + (p1 - p2) * 3 - p0;
  const ScalarPoint b = p2 - Times2(p1) + p0;
  return a * t + b;
}

template <typename T>
void BubbleSort(T array[], int count) {
  for (int i = count - 1; i > 0; --i) {
    for (int j = i; j > 0; --j) {
      if (array[j] < array[j - 1]) std::swap(array[j], array[j - 1]);
    }
  }
}

// Given an array and count, remove all pair-wise duplicates from the array,
// keeping the existing sorting, and return the new count.
int CollapsDuplicates(float array[], int count) {
  for (int n = count; n > 1; --n) {
    if (array[0] == array[1]) {
      for (int i = 1; i < n; ++i) array[i - 1] = array[i];
      count -= 1;
    } else {
      array += 1;
    }
  }
  return count;
}

float ScalarCubeRoot(float x) {
  return std::pow(x, 0.3333333f);
}

constexpr float kScalarPI = 3.14159265f;

// Solve coeff(t) == 0, returning the number of roots that lie withing
// 0 < t < 1. coeff[0]t^3 + coeff[1]t^2 + coeff[2]t + coeff[3]
//
// Eliminates repeated roots (so that all t_values are distinct, and are always
// in increasing order.
int SolveCubicPoly(const float coeff[4], float t_values[3]) {
  if (point::ScalarNearlyZero(coeff[0])) { // we're just a quadratic
    return FindUnitQuadRoots(coeff[1], coeff[2], coeff[3], t_values);
  }

  float a, b, c, q, r;
  {
    const float inva = 1 / coeff[0];
    a = coeff[1] * inva;
    b = coeff[2] * inva;
    c = coeff[3] * inva;
  }
  q = (a * a - b * 3) / 9;
  r = (2 * a * a * a - 9 * a * b + 27 * c) / 54;

  const float q3 = q * q * q;
  const float r2_minus_q3 = r * r - q3;
  const float adiv3 = a / 3;

  if (r2_minus_q3 < 0) { // we have 3 real roots
    // the divide/root can, due to finite precisions, be slightly outside of -1...1
    const float theta = std::acos(std::clamp(r / std::sqrt(q3), -1.0f, 1.0f));
    const float neg2_root_q = -2 * std::sqrt(q);

    const auto pin = [](float value) {
      // SkTPin returns the lower bound for NaN.
      return std::max(0.0f, std::min(value, 1.0f));
    };
    t_values[0] = pin(neg2_root_q * std::cos(theta / 3) - adiv3);
    t_values[1] = pin(neg2_root_q * std::cos((theta + 2 * kScalarPI) / 3) - adiv3);
    t_values[2] = pin(neg2_root_q * std::cos((theta - 2 * kScalarPI) / 3) - adiv3);

    // now sort the roots
    BubbleSort(t_values, 3);
    return CollapsDuplicates(t_values, 3);
  }
  // we have 1 real root
  float big_a = std::fabs(r) + std::sqrt(r2_minus_q3);
  big_a = ScalarCubeRoot(big_a);
  if (r > 0) big_a = -big_a;
  if (big_a != 0) big_a += q / big_a;
  t_values[0] = std::max(0.0f, std::min(big_a - adiv3, 1.0f));
  return 1;
}

// Looking for F' dot F'' == 0
//
// A = b - a
// B = c - 2b + a
// C = d - 3c + 3b - a
//
// F' = 3Ct^2 + 6Bt + 3A
// F'' = 6Ct + 6B
//
// F' dot F'' -> CCt^3 + 3BCt^2 + (2BB + CA)t + AB
void FormulateF1DotF2(const float src[], float coeff[4]) {
  const float a = src[2] - src[0];
  const float b = src[4] - 2 * src[2] + src[0];
  const float c = src[6] + 3 * (src[2] - src[4]) - src[0];
  coeff[0] = c * c;
  coeff[1] = 3 * b * c;
  coeff[2] = 2 * b * b + c * a;
  coeff[3] = a * b;
}

// Returns a constant proportional to the dimensions of the cubic.
float CalcCubicPrecision(const ScalarPoint src[4]) {
  return (point::DistanceToSqd(src[1], src[0]) + point::DistanceToSqd(src[2], src[1]) +
          point::DistanceToSqd(src[3], src[2])) *
         1e-8f;
}

// Returns true if both points src[test_index], src[test_index+1] are in the
// same half plane defined by the line segment src[line_index],
// src[line_index+1].
bool OnSameSide(const ScalarPoint src[4], int test_index, int line_index) {
  const ScalarPoint origin = src[line_index];
  const ScalarPoint line = src[line_index + 1] - origin;
  float crosses[2];
  for (int index = 0; index < 2; ++index) {
    const ScalarPoint test_line = src[test_index + index] - origin;
    crosses[index] = line.Cross(test_line);
  }
  return crosses[0] * crosses[1] >= 0;
}

// We only interpolate one dimension at a time (the first, at +0, +3, +6).
void P3dInterp(const float src[7], float dst[7], float t) {
  const float ab = src[0] + (src[3] - src[0]) * t;
  const float bc = src[3] + (src[6] - src[3]) * t;
  dst[0] = ab;
  dst[3] = ab + (bc - ab) * t;
  dst[6] = bc;
}

float SubdivideWValue(float w) {
  return std::sqrt(0.5f + w * 0.5f);
}

bool Between(float a, float b, float c) {
  return (a - b) * (c - b) <= 0;
}

ScalarPoint* Subdivide(const Conic& src, ScalarPoint pts[], int level) {
  if (0 == level) {
    pts[0] = src.pts[1];
    pts[1] = src.pts[2];
    return pts + 2;
  }
  Conic dst[2];
  src.Chop(dst);
  const float start_y = src.pts[0].y;
  const float end_y = src.pts[2].y;
  if (Between(start_y, src.pts[1].y, end_y)) {
    // If the input is monotonic and the output is not, the scan converter
    // hangs. Ensure that the chopped conics maintain their y-order.
    const float mid_y = dst[0].pts[2].y;
    if (!Between(start_y, mid_y, end_y)) {
      // If the computed midpoint is outside the ends, move it to the closer one.
      const float closer_y = std::fabs(mid_y - start_y) < std::fabs(mid_y - end_y) ? start_y : end_y;
      dst[0].pts[2].y = dst[1].pts[0].y = closer_y;
    }
    if (!Between(start_y, dst[0].pts[1].y, dst[0].pts[2].y)) {
      // If the 1st control is not between the start and end, put it at the
      // start. This also reduces the quad to a line.
      dst[0].pts[1].y = start_y;
    }
    if (!Between(dst[1].pts[0].y, dst[1].pts[1].y, end_y)) {
      // If the 2nd control is not between the start and end, put it at the
      // end. This also reduces the quad to a line.
      dst[1].pts[1].y = end_y;
    }
  }
  --level;
  pts = Subdivide(dst[0], pts, level);
  return Subdivide(dst[1], pts, level);
}

bool AreFinite(const ScalarPoint pts[], int count) {
  float accum = 0;
  for (int i = 0; i < count; ++i) {
    accum *= pts[i].x;
    accum *= pts[i].y;
  }
  return accum == 0;
}

} // namespace

// From Numerical Recipes in C.
//
// Q = -1/2 (B + sign(B) sqrt[B*B - 4*A*C])
// x1 = Q / A
// x2 = C / Q
int FindUnitQuadRoots(float a, float b, float c, float roots[2]) {
  if (a == 0) return ValidUnitDivide(-c, b, roots);

  float* r = roots;
  // use doubles so we don't overflow temporarily trying to compute R
  double dr = static_cast<double>(b) * b - 4 * static_cast<double>(a) * c;
  if (dr < 0) return 0;
  dr = std::sqrt(dr);
  const float big_r = static_cast<float>(dr);
  if (!std::isfinite(big_r)) return 0;

  const float q = (b < 0) ? -(b - big_r) / 2 : -(b + big_r) / 2;
  r += ValidUnitDivide(q, a, r);
  r += ValidUnitDivide(c, q, r);
  if (r - roots == 2) {
    if (roots[0] > roots[1]) {
      std::swap(roots[0], roots[1]);
    } else if (roots[0] == roots[1]) { // nearly-equal?
      r -= 1;                          // skip the double root
    }
  }
  return static_cast<int>(r - roots);
}

void EvalQuadAt(const ScalarPoint src[3], float t, ScalarPoint* pt, ScalarPoint* tangent) {
  if (pt) *pt = EvalQuadAt(src, t);
  if (tangent) *tangent = EvalQuadTangentAt(src, t);
}

ScalarPoint EvalQuadAt(const ScalarPoint src[3], float t) {
  return QuadCoeff(src).Eval(t);
}

ScalarPoint EvalQuadTangentAt(const ScalarPoint src[3], float t) {
  // The derivative equation is 2(b - a +(a - 2b +c)t). This returns a zero
  // tangent vector when t is 0 or 1, and the control point is equal to the
  // end point. In this case, use the quad end points to compute the tangent.
  if ((t == 0 && src[0] == src[1]) || (t == 1 && src[1] == src[2])) return src[2] - src[0];
  const ScalarPoint p0 = src[0];
  const ScalarPoint p1 = src[1];
  const ScalarPoint p2 = src[2];
  const ScalarPoint b = p1 - p0;
  const ScalarPoint a = p2 - p1 - b;
  const ScalarPoint value = a * t + b;
  return value + value;
}

void ChopQuadAt(const ScalarPoint src[3], ScalarPoint dst[5], float t) {
  const ScalarPoint p0 = src[0];
  const ScalarPoint p1 = src[1];
  const ScalarPoint p2 = src[2];
  const ScalarPoint p01 = Interp(p0, p1, t);
  const ScalarPoint p12 = Interp(p1, p2, t);
  dst[0] = p0;
  dst[1] = p01;
  dst[2] = Interp(p01, p12, t);
  dst[3] = p12;
  dst[4] = p2;
}

void ChopQuadAtHalf(const ScalarPoint src[3], ScalarPoint dst[5]) {
  ChopQuadAt(src, dst, 0.5f);
}

//  Fx' Fx'' + Fy' Fy'' = 0
//
//  t = - (Ax Bx + Ay By) / (Bx ^ 2 + By ^ 2)
float FindQuadMaxCurvature(const ScalarPoint src[3]) {
  const float ax = src[1].x - src[0].x;
  const float ay = src[1].y - src[0].y;
  const float bx = src[0].x - src[1].x - src[1].x + src[2].x;
  const float by = src[0].y - src[1].y - src[1].y + src[2].y;

  float numer = -(ax * bx + ay * by);
  float denom = bx * bx + by * by;
  if (denom < 0) {
    numer = -numer;
    denom = -denom;
  }
  if (numer <= 0) return 0;
  if (numer >= denom) return 1; // Also catches denom=0.
  return numer / denom;
}

void EvalCubicAt(const ScalarPoint src[4], float t, ScalarPoint* loc, ScalarPoint* tangent, ScalarPoint* curvature) {
  if (loc) *loc = CubicCoeff(src).Eval(t);
  if (tangent) {
    // The derivative equation returns a zero tangent vector when t is 0 or 1,
    // and the adjacent control point is equal to the end point. In this case,
    // use the next control point or the end points to compute the tangent.
    if ((t == 0 && src[0] == src[1]) || (t == 1 && src[2] == src[3])) {
      if (t == 0) *tangent = src[2] - src[0];
      else *tangent = src[3] - src[1];
      if (!tangent->x && !tangent->y) *tangent = src[3] - src[0];
    } else {
      *tangent = EvalCubicDerivative(src, t);
    }
  }
  if (curvature) *curvature = EvalCubic2ndDerivative(src, t);
}

void ChopCubicAt(const ScalarPoint src[4], ScalarPoint dst[7], float t) {
  if (t == 1) {
    std::memcpy(dst, src, sizeof(ScalarPoint) * 4);
    dst[4] = dst[5] = dst[6] = src[3];
    return;
  }
  const ScalarPoint p0 = src[0];
  const ScalarPoint p1 = src[1];
  const ScalarPoint p2 = src[2];
  const ScalarPoint p3 = src[3];

  const ScalarPoint ab = UncheckedMix(p0, p1, t);
  const ScalarPoint bc = UncheckedMix(p1, p2, t);
  const ScalarPoint cd = UncheckedMix(p2, p3, t);
  const ScalarPoint abc = UncheckedMix(ab, bc, t);
  const ScalarPoint bcd = UncheckedMix(bc, cd, t);
  const ScalarPoint abcd = UncheckedMix(abc, bcd, t);

  dst[0] = p0;
  dst[1] = ab;
  dst[2] = abc;
  dst[3] = abcd;
  dst[4] = bcd;
  dst[5] = cd;
  dst[6] = p3;
}

void ChopCubicAtHalf(const ScalarPoint src[4], ScalarPoint dst[7]) {
  ChopCubicAt(src, dst, 0.5f);
}

// http://www.faculty.idc.ac.il/arik/quality/appendixA.html
//
// Inflection means that curvature is zero. Curvature is
// [F' x F''] / [F'^3]. So we solve F'x X F''y - F'y X F''y == 0. After some
// canceling of the cubic term, we get
// A = b - a
// B = c - 2b + a
// C = d - 3c + 3b - a
// (BxCy - ByCx)t^2 + (AxCy - AyCx)t + AxBy - AyBx == 0
int FindCubicInflections(const ScalarPoint src[4], float t_values[2]) {
  const float ax = src[1].x - src[0].x;
  const float ay = src[1].y - src[0].y;
  const float bx = src[2].x - 2 * src[1].x + src[0].x;
  const float by = src[2].y - 2 * src[1].y + src[0].y;
  const float cx = src[3].x + 3 * (src[1].x - src[2].x) - src[0].x;
  const float cy = src[3].y + 3 * (src[1].y - src[2].y) - src[0].y;
  return FindUnitQuadRoots(bx * cy - by * cx, ax * cy - ay * cx, ax * by - ay * bx, t_values);
}

int FindCubicMaxCurvature(const ScalarPoint src[4], float t_values[3]) {
  float coeff_x[4], coeff_y[4];
  const float xs[7] = {src[0].x, src[0].y, src[1].x, src[1].y, src[2].x, src[2].y, src[3].x};
  const float ys[7] = {src[0].y, src[1].x, src[1].y, src[2].x, src[2].y, src[3].x, src[3].y};
  FormulateF1DotF2(xs, coeff_x);
  FormulateF1DotF2(ys, coeff_y);
  for (int i = 0; i < 4; i++) coeff_x[i] += coeff_y[i];
  return SolveCubicPoly(coeff_x, t_values);
}

// Return location (in t) of cubic cusp, if there is one.
// Note that classify cubic code does not reliably return all cusp'd cubics,
// so it is not called here.
float FindCubicCusp(const ScalarPoint src[4]) {
  // When the adjacent control point matches the end point, it behaves as if
  // the cubic has a cusp: there's a point of max curvature where the
  // derivative goes to zero. Ideally, this would be where t is zero or one,
  // but math error makes not so. It is not uncommon to create cubics this
  // way; skip them.
  if (src[0] == src[1]) return -1;
  if (src[2] == src[3]) return -1;
  // Cubics only have a cusp if the line segments formed by the control and
  // end points cross. Detect crossing if line ends are on opposite sides of
  // plane formed by the other line.
  if (OnSameSide(src, 0, 2) || OnSameSide(src, 2, 0)) return -1;
  // Cubics may have multiple points of maximum curvature, although at most
  // only one is a cusp.
  float max_curvature[3];
  const int roots = FindCubicMaxCurvature(src, max_curvature);
  for (int index = 0; index < roots; ++index) {
    const float test_t = max_curvature[index];
    if (0 >= test_t || test_t >= 1) continue; // no need to consider max curvature on the end
    // A cusp is at the max curvature, and also has a derivative close to
    // zero. Choose the 'close to zero' meaning by comparing the derivative
    // length with the overall cubic size.
    const ScalarPoint d_pt = EvalCubicDerivative(src, test_t);
    const float d_pt_magnitude = point::LengthSqd(d_pt);
    const float precision = CalcCubicPrecision(src);
    if (d_pt_magnitude < precision) {
      // All three max curvature t values may be close to the cusp; return
      // the first one.
      return test_t;
    }
  }
  return -1;
}

bool Conic::ChopAt(float t, Conic dst[2]) const {
  // ratquad_mapTo3D.
  float tmp[9] = {pts[0].x, pts[0].y, 1, pts[1].x * w, pts[1].y * w, w, pts[2].x, pts[2].y, 1};
  float tmp2[9];
  P3dInterp(&tmp[0], &tmp2[0], t);
  P3dInterp(&tmp[1], &tmp2[1], t);
  P3dInterp(&tmp[2], &tmp2[2], t);

  const auto project_down = [](const float* src) {
    return ScalarPoint{src[0] / src[2], src[1] / src[2]};
  };
  dst[0].pts[0] = pts[0];
  dst[0].pts[1] = project_down(&tmp2[0]);
  dst[0].pts[2] = project_down(&tmp2[3]);
  dst[1].pts[0] = dst[0].pts[2];
  dst[1].pts[1] = project_down(&tmp2[6]);
  dst[1].pts[2] = pts[2];

  // to put in "standard form", where w0 and w2 are both 1, we compute the
  // new w1 as sqrt(w1*w1/w0*w2)
  // or
  // w1 /= sqrt(w0*w2)
  //
  // However, in our case, we know that for dst[0]:
  //     w0 == 1, and for dst[1], w2 == 1
  const float root = std::sqrt(tmp2[5]);
  dst[0].w = tmp2[2] / root;
  dst[1].w = tmp2[8] / root;
  return AreFinite(dst[0].pts, 3) && AreFinite(dst[1].pts, 3) && std::isfinite(dst[0].w) && std::isfinite(dst[1].w);
}

void Conic::ChopAt(float t1, float t2, Conic* dst) const {
  if (0 == t1 || 1 == t2) {
    if (0 == t1 && 1 == t2) {
      *dst = *this;
      return;
    }
    Conic pair[2];
    if (ChopAt(t1 ? t1 : t2, pair)) {
      *dst = pair[t1 ? 1 : 0];
      return;
    }
  }
  const ConicCoeff coeff(*this);
  const ScalarPoint a_xy = coeff.numer.Eval(t1);
  const ScalarPoint a_zz = coeff.denom.Eval(t1);
  const float mid_tt = (t1 + t2) / 2;
  const ScalarPoint d_xy = coeff.numer.Eval(mid_tt);
  const ScalarPoint d_zz = coeff.denom.Eval(mid_tt);
  const ScalarPoint c_xy = coeff.numer.Eval(t2);
  const ScalarPoint c_zz = coeff.denom.Eval(t2);
  const ScalarPoint b_xy = Times2(d_xy) - (a_xy + c_xy) * 0.5f;
  const ScalarPoint b_zz = Times2(d_zz) - (a_zz + c_zz) * 0.5f;
  dst->pts[0] = {a_xy.x / a_zz.x, a_xy.y / a_zz.y};
  dst->pts[1] = {b_xy.x / b_zz.x, b_xy.y / b_zz.y};
  dst->pts[2] = {c_xy.x / c_zz.x, c_xy.y / c_zz.y};
  dst->w = b_zz.x / std::sqrt(a_zz.x * c_zz.x);
}

ScalarPoint Conic::EvalAt(float t) const {
  return ConicCoeff(*this).Eval(t);
}

ScalarPoint Conic::EvalTangentAt(float t) const {
  // The derivative equation returns a zero tangent vector when t is 0 or 1,
  // and the control point is equal to the end point. In this case, use the
  // conic endpoints to compute the tangent.
  if ((t == 0 && pts[0] == pts[1]) || (t == 1 && pts[1] == pts[2])) return pts[2] - pts[0];
  const ScalarPoint p0 = pts[0];
  const ScalarPoint p1 = pts[1];
  const ScalarPoint p2 = pts[2];
  const ScalarPoint p20 = p2 - p0;
  const ScalarPoint p10 = p1 - p0;
  const ScalarPoint c = p10 * w;
  const ScalarPoint a = p20 * w - p20;
  const ScalarPoint b = p20 - c - c;
  return QuadCoeff(a, b, c).Eval(t);
}

void Conic::EvalAt(float t, ScalarPoint* pt, ScalarPoint* tangent) const {
  if (pt) *pt = EvalAt(t);
  if (tangent) *tangent = EvalTangentAt(t);
}

void Conic::Chop(Conic dst[2]) const {
  // Observe that scale will always be smaller than 1 because w > 0.
  const float scale = 1 / (1 + w);

  // The subdivided control points below are the sums of the following three
  // terms. Because the terms are multiplied by something <1, and the
  // resulting control points lie within the control points of the original
  // then the terms and the sums below will not overflow. Note that w * scale
  // approaches 1 as w becomes very large.
  const ScalarPoint t0 = pts[0] * scale;
  const ScalarPoint t1 = pts[1] * (w * scale);
  const ScalarPoint t2 = pts[2] * scale;

  // Calculate the subdivided control points
  const ScalarPoint p1 = t0 + t1;
  const ScalarPoint p3 = t1 + t2;

  // p2 = (t0 + 2*t1 + t2) / 2. Divide the terms by 2 before the sum to keep
  // the sum for p2 from overflowing.
  const ScalarPoint p2 = t0 * 0.5f + t1 + t2 * 0.5f;

  dst[0].pts[0] = pts[0];
  dst[0].pts[1] = p1;
  dst[0].pts[2] = p2;
  dst[1].pts[0] = p2;
  dst[1].pts[1] = p3;
  dst[1].pts[2] = pts[2];

  // Update w.
  dst[0].w = dst[1].w = SubdivideWValue(w);
}

// "High order approximation of conic sections by quadratic splines"
//     by Michael Floater, 1993
int Conic::ComputeQuadPOW2(float tol) const {
  if (tol < 0 || !std::isfinite(tol) || !AreFinite(pts, 3)) return 0;

  const float a = w - 1;
  const float k = a / (4 * (2 + a));
  const float x = k * (pts[0].x - 2 * pts[1].x + pts[2].x);
  const float y = k * (pts[0].y - 2 * pts[1].y + pts[2].y);

  float error = std::sqrt(x * x + y * y);
  int pow2;
  for (pow2 = 0; pow2 < kMaxConicToQuadPOW2; ++pow2) {
    if (error <= tol) break;
    error *= 0.25f;
  }
  return pow2;
}

int Conic::ChopIntoQuadsPOW2(ScalarPoint dst[], int pow2) const {
  *dst = pts[0];
  if (pow2 == kMaxConicToQuadPOW2) { // If an extreme weight generates many quads ...
    Conic chopped[2];
    Chop(chopped);
    // check to see if the first chop generates a pair of lines
    if (point::EqualsWithinTolerance(chopped[0].pts[1], chopped[0].pts[2]) &&
        point::EqualsWithinTolerance(chopped[1].pts[0], chopped[1].pts[1])) {
      dst[1] = dst[2] = dst[3] = chopped[0].pts[1]; // set ctrl == end to make lines
      dst[4] = chopped[1].pts[2];
      pow2 = 1;
      goto common_finite_pt_check;
    }
  }
  Subdivide(*this, dst + 1, pow2);
common_finite_pt_check:
  const int quad_count = 1 << pow2;
  const int pt_count = 2 * quad_count + 1;
  if (!AreFinite(dst, pt_count)) {
    // if we generated a non-finite, pin ourselves to the middle of the hull,
    // as our first and last are already on the first/last pts of the hull.
    for (int i = 1; i < pt_count - 1; ++i) dst[i] = pts[1];
  }
  return 1 << pow2;
}

int Conic::BuildUnitArc(ScalarPoint u_start, ScalarPoint u_stop, RotationDirection dir,
                        const ScalarMatrix* user_matrix, Conic dst[kMaxConicsForArc]) {
  // rotate by x,y so that u_start is (1.0)
  const float x = u_start.Dot(u_stop);
  float y = u_start.Cross(u_stop);

  const float abs_y = std::fabs(y);

  // check for (effectively) coincident vectors
  // this can happen if our angle is nearly 0 or nearly 180 (y == 0)
  // ... we use the dot-prod to distinguish between 0 and 180 (x > 0)
  if (abs_y <= kScalarNearlyZero && x > 0 &&
      ((y >= 0 && RotationDirection::kCW == dir) || (y <= 0 && RotationDirection::kCCW == dir))) {
    return 0;
  }

  if (dir == RotationDirection::kCCW) y = -y;

  // We decide to use 1-conic per quadrant of a circle. What quadrant does
  // [xy] lie in?
  //      0 == [0  .. 90)
  //      1 == [90 ..180)
  //      2 == [180..270)
  //      3 == [270..360)
  int quadrant = 0;
  if (0 == y) {
    quadrant = 2; // 180
  } else if (0 == x) {
    quadrant = y > 0 ? 1 : 3; // 90 : 270
  } else {
    if (y < 0) quadrant += 2;
    if ((x < 0) != (y < 0)) quadrant += 1;
  }

  static const ScalarPoint kQuadrantPts[] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
  constexpr float kQuadrantWeight = 0.707106781f;

  int conic_count = quadrant;
  for (int i = 0; i < conic_count; ++i) dst[i].Set(&kQuadrantPts[i * 2], kQuadrantWeight);

  // Now compute any remaing (sub-90-degree) arc for the last conic
  const ScalarPoint final_p = {x, y};
  const ScalarPoint& last_q = kQuadrantPts[quadrant * 2]; // will already be a unit-vector
  const float dot = last_q.Dot(final_p);
  if (std::isnan(dot)) return 0;

  if (dot < 1) {
    ScalarPoint off_curve = {last_q.x + x, last_q.y + y};
    // compute the bisector vector, and then rescale to be the off-curve
    // point. we compute its length from cos(theta/2) = length / 1, using half
    // angle identity we get length = sqrt(2 / (1 + cos(theta)). We already
    // have cos() when to computed the dot. This is nice, since our computed
    // weight is cos(theta/2) as well!
    const float cos_theta_over2 = std::sqrt((1 + dot) / 2);
    point::SetLength(&off_curve, 1 / cos_theta_over2);
    if (!point::EqualsWithinTolerance(last_q, off_curve)) {
      dst[conic_count].Set(last_q, off_curve, final_p, cos_theta_over2);
      conic_count += 1;
    }
  }

  // now handle counter-clockwise and the initial unitStart rotation
  ScalarMatrix matrix;
  matrix.SetSinCos(u_start.y, u_start.x);
  if (dir == RotationDirection::kCCW) matrix.PreScale(1, -1);
  if (user_matrix) matrix.PostConcat(*user_matrix);
  for (int i = 0; i < conic_count; ++i) {
    for (ScalarPoint& p : dst[i].pts) p = matrix.MapPoint(p);
  }
  return conic_count;
}

} // namespace bkfont
