// Ported from: skia/src/base/SkBezierCurves.cpp
// Ported from: skia/src/base/SkQuads.cpp
// Ported from: skia/src/base/SkCubics.cpp
// Ported from: skia/src/base/SkFloatingPoint.cpp

#include "bezier_curves.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numbers>

namespace bkit {

namespace {

// Return the positive magnitude of a double.
// * normalized - given 1.bbb...bbb x 2^e return 2^e.
// * subnormal - return 0.
// * nan & infinity - return infinity
double Magnitude(double a) {
  constexpr std::int64_t kExtractMagnitude = 0b0'11111111111'0000000000000000000000000000000000000000000000000000;
  std::int64_t bits;
  std::memcpy(&bits, &a, sizeof(bits));
  bits &= kExtractMagnitude;
  double out;
  std::memcpy(&out, &bits, sizeof(out));
  return out;
}

// Solve 0 = M * x + B. If M is 0, there are no solutions, unless B is also 0,
// in which case there are infinite solutions, so we just return 1 of them.
int SolveLinear(const double M, const double B, double solution[2]) {
  if (DoubleNearlyZero(M)) {
    solution[0] = 0;
    if (DoubleNearlyZero(B)) {
      return 1;
    }
    return 0;
  }
  solution[0] = -B / M;
  if (!std::isfinite(solution[0])) {
    return 0;
  }
  return 1;
}

// When B >> A, then the x^2 component doesn't contribute much to the output,
// so the second root will be very large, but have massive round off error.
// Because of the round off error, the second root will not evaluate to zero
// when substituted back into the quadratic equation. In the situation when B
// >> A, then just treat the quadratic as a linear equation.
bool CloseToLinear(double A, double B) {
  if (A != 0) {
    // Return if B is much bigger than A.
    return std::abs(B / A) >= 1.0e+16;
  }

  // Otherwise A is zero, and the quadratic is linear.
  return true;
}

double ZeroIfTiny(double x) {
  return DoubleNearlyZero(x) ? 0 : x;
}

bool NearlyEqual(double x, double y) {
  if (DoubleNearlyZero(x)) {
    return DoubleNearlyZero(y);
  }
  return DoublesNearlyEqualUlps(x, y);
}

// When the A coefficient of a cubic is close to 0, there can be floating
// point error that arises from computing a very large root. In those cases,
// we would rather be precise about the smaller 2 roots, so we have this
// arbitrary cutoff for when A is really small or small compared to B.
bool CloseToAQuadratic(double A, double B) {
  if (DoubleNearlyZero(B)) {
    return DoubleNearlyZero(A);
  }
  return std::abs(A / B) < 1.0e-7;
}

// Pin to 0 or 1 if within half a float ulp of 0 or 1.
double PinTRange(double t) {
  // The ULPs around 0 are tiny compared to the ULPs around 1. Shift to 1 to
  // use the same size ULPs.
  if (static_cast<float>(t + 1.0) == 1.0f) {
    return 0.0;
  } else if (static_cast<float>(t) == 1.0f) {
    return 1.0;
  }
  return t;
}

struct DPoint {
  double x;
  double y;
};

DPoint ToDPoint(ScalarPoint p) {
  return {p.x, p.y};
}

} // namespace

bool DoubleNearlyZero(double a) {
  return a == 0 || std::fabs(a) < std::numeric_limits<float>::epsilon();
}

bool DoublesNearlyEqualUlps(double a, double b, unsigned max_ulps_diff) {
  // The maximum magnitude to construct the ulp tolerance. The proper magnitude
  // for subnormal numbers is minMagnitude, which is 2^-1021, so if a and b are
  // subnormal (having a magnitude of 0) use minMagnitude. If a or b are
  // infinity or nan, then maxMagnitude will be +infinity. This means the
  // tolerance will also be infinity, but the expression b - a below will
  // either be NaN or infinity, so a tolerance of infinity doesn't matter.
  constexpr double kMinMagnitude = std::numeric_limits<double>::min();
  const double max_magnitude = std::max(std::max(Magnitude(a), kMinMagnitude), Magnitude(b));

  // Given a magnitude, this is the factor that generates the ulp for that
  // magnitude. In numbers, 2 ^ (-precision + 1) = 2 ^ -52.
  constexpr double kUlpFactor = std::numeric_limits<double>::epsilon();

  // The tolerance in ULPs given the maxMagnitude. Because the return statement
  // must use < for comparison instead of <= to correctly handle infinities,
  // bump maxUlpsDiff up to get the full maxUlpsDiff range.
  const double tolerance = max_magnitude * (kUlpFactor * (max_ulps_diff + 1));

  // The expression a == b is mainly for handling infinities, but it also
  // catches the exact equals.
  return a == b || std::abs(b - a) < tolerance;
}

double Quads::Discriminant(const double a, const double b, const double c) {
  const double b2 = b * b;
  const double ac = a * c;

  // Calculate the rough discriminate which may suffer from a loss in
  // precision due to b2 and ac being too close.
  const double rough_discriminant = b2 - ac;

  // If 3 * |B2 - AC| >= AC + B2 holds, then the roughDiscriminant has 2-bits
  // of rounding error or less and can be used.
  if (3 * std::abs(rough_discriminant) >= b2 + ac) {
    return rough_discriminant;
  }

  // Use the extra internal precision afforded by fma to calculate the
  // rounding error for b^2 and ac.
  const double b2_rounding_error = std::fma(b, b, -b2);
  const double ac_rounding_error = std::fma(a, c, -ac);

  // Add the total rounding error back into the discriminant guess.
  const double discriminant = (b2 - ac) + (b2_rounding_error - ac_rounding_error);
  return discriminant;
}

Quads::RootResult Quads::Roots(double A, double B, double C) {
  const double discriminant = Discriminant(A, B, C);

  if (A == 0) {
    double root;
    if (B == 0) {
      if (C == 0) {
        root = std::numeric_limits<double>::infinity();
      } else {
        root = std::numeric_limits<double>::quiet_NaN();
      }
    } else {
      // Solve -2*B*x + C == 0; x = c/(2*b).
      root = C / (2 * B);
    }
    return {discriminant, root, root};
  }

  if (discriminant == 0) {
    return {discriminant, B / A, B / A};
  }

  if (discriminant > 0) {
    const double D = std::sqrt(discriminant);
    const double R = B > 0 ? B + D : B - D;
    return {discriminant, R / A, C / R};
  }

  // The discriminant is negative or is not finite.
  return {discriminant, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
}

int Quads::RootsReal(const double A, const double B, const double C, double solution[2]) {
  if (CloseToLinear(A, B)) {
    return SolveLinear(B, C, solution);
  }

  auto [discriminant, root0, root1] = Roots(A, -0.5 * B, C);

  // Handle invariants to mesh with existing code from here on.
  if (!std::isfinite(discriminant) || discriminant < 0) {
    return 0;
  }

  int roots = 0;
  if (const double r0 = ZeroIfTiny(root0); std::isfinite(r0)) {
    solution[roots++] = r0;
  }
  if (const double r1 = ZeroIfTiny(root1); std::isfinite(r1)) {
    solution[roots++] = r1;
  }

  if (roots == 2 && DoublesNearlyEqualUlps(solution[0], solution[1])) {
    roots = 1;
  }

  return roots;
}

double Quads::EvalAt(double A, double B, double C, double t) {
  // Use fused-multiply-add to reduce the amount of round-off error between
  // terms.
  return std::fma(std::fma(A, t, B), t, C);
}

int Cubics::RootsReal(double A, double B, double C, double D, double solution[3]) {
  if (CloseToAQuadratic(A, B)) {
    return Quads::RootsReal(B, C, D, solution);
  }
  if (DoubleNearlyZero(D)) { // 0 is one root
    int num = Quads::RootsReal(A, B, C, solution);
    for (int i = 0; i < num; ++i) {
      if (DoubleNearlyZero(solution[i])) {
        return num;
      }
    }
    solution[num++] = 0;
    return num;
  }
  if (DoubleNearlyZero(A + B + C + D)) { // 1 is one root
    int num = Quads::RootsReal(A, A + B, -D, solution);
    for (int i = 0; i < num; ++i) {
      if (DoublesNearlyEqualUlps(solution[i], 1)) {
        return num;
      }
    }
    solution[num++] = 1;
    return num;
  }
  double a, b, c;
  {
    // If A is zero (e.g. B was nan and thus close_to_a_quadratic was false),
    // we will temporarily have infinities rolling about, but will catch that
    // when checking R2MinusQ3. sk_ieee_double_divide.
    double inv_a = 1 / A;
    a = B * inv_a;
    b = C * inv_a;
    c = D * inv_a;
  }
  double a2 = a * a;
  double Q = (a2 - b * 3) / 9;
  double R = (2 * a2 * a - 9 * a * b + 27 * c) / 54;
  double R2 = R * R;
  double Q3 = Q * Q * Q;
  double r2_minus_q3 = R2 - Q3;
  // If one of R2 Q3 is infinite or nan, subtracting them will also be
  // infinite/nan. If both are infinite or nan, the subtraction will be nan.
  // In either case, we have no finite roots.
  if (!std::isfinite(r2_minus_q3)) {
    return 0;
  }
  double adiv3 = a / 3;
  double r;
  double* roots = solution;
  if (r2_minus_q3 < 0) { // we have 3 real roots
    // the divide/root can, due to finite precisions, be slightly outside of
    // -1...1
    const double theta = std::acos(std::clamp(R / std::sqrt(Q3), -1., 1.));
    const double neg2_root_q = -2 * std::sqrt(Q);

    r = neg2_root_q * std::cos(theta / 3) - adiv3;
    *roots++ = r;

    r = neg2_root_q * std::cos((theta + 2 * std::numbers::pi) / 3) - adiv3;
    if (!NearlyEqual(solution[0], r)) {
      *roots++ = r;
    }
    r = neg2_root_q * std::cos((theta - 2 * std::numbers::pi) / 3) - adiv3;
    if (!NearlyEqual(solution[0], r) &&
        (roots - solution == 1 || !NearlyEqual(solution[1], r))) {
      *roots++ = r;
    }
  } else { // we have 1 real root
    const double sqrt_r2_minus_q3 = std::sqrt(r2_minus_q3);
    A = std::fabs(R) + sqrt_r2_minus_q3;
    A = std::cbrt(A); // cube root
    if (R > 0) {
      A = -A;
    }
    if (!DoubleNearlyZero(A)) {
      A += Q / A;
    }
    r = A - adiv3;
    *roots++ = r;
    if (!DoubleNearlyZero(R2) && DoublesNearlyEqualUlps(R2, Q3)) {
      r = -A / 2 - adiv3;
      if (!NearlyEqual(solution[0], r)) {
        *roots++ = r;
      }
    }
  }
  return static_cast<int>(roots - solution);
}

double Cubics::EvalAt(double A, double B, double C, double D, double t) {
  return std::fma(t, std::fma(t, std::fma(t, A, B), C), D);
}

std::span<const float> BezierCubic::IntersectWithHorizontalLine(std::span<const ScalarPoint> control_points, float y_intercept,
                                                                float intersection_storage[3]) {
  const DPoint P0 = ToDPoint(control_points[0]);
  const DPoint P1 = ToDPoint(control_points[1]);
  const DPoint P2 = ToDPoint(control_points[2]);
  const DPoint P3 = ToDPoint(control_points[3]);

  const DPoint A = {-P0.x + 3 * P1.x - 3 * P2.x + P3.x, -P0.y + 3 * P1.y - 3 * P2.y + P3.y};
  const DPoint B = {3 * P0.x - 6 * P1.x + 3 * P2.x, 3 * P0.y - 6 * P1.y + 3 * P2.y};
  const DPoint C = {-3 * P0.x + 3 * P1.x, -3 * P0.y + 3 * P1.y};
  const DPoint D = P0;

  return Intersect(A.x, B.x, C.x, D.x, A.y, B.y, C.y, D.y, y_intercept, intersection_storage);
}

std::span<const float> BezierCubic::Intersect(double AX, double BX, double CX, double DX,
                                              double AY, double BY, double CY, double DY,
                                              float to_intersect, float intersections_storage[3]) {
  double roots[3];
  const int root_count = Cubics::RootsReal(AY, BY, CY, DY - to_intersect, roots);

  int intersection_count = 0;
  for (int i = 0; i < root_count; ++i) {
    const double pinned_t = PinTRange(roots[i]);
    if (0 <= pinned_t && pinned_t <= 1) {
      intersections_storage[intersection_count++] = static_cast<float>(Cubics::EvalAt(AX, BX, CX, DX, pinned_t));
    }
  }

  return {intersections_storage, static_cast<std::size_t>(intersection_count)};
}

std::span<const float> BezierQuad::IntersectWithHorizontalLine(std::span<const ScalarPoint> control_points, float y_intercept,
                                                               float intersection_storage[2]) {
  const DPoint p0 = ToDPoint(control_points[0]);
  const DPoint p1 = ToDPoint(control_points[1]);
  const DPoint p2 = ToDPoint(control_points[2]);

  // Calculate A, B, C using doubles to reduce round-off error.
  const DPoint A = {p0.x - 2 * p1.x + p2.x, p0.y - 2 * p1.y + p2.y};
  // Remember we are generating the polynomial in the form A*t^2 -2*B*t + C,
  // so the factor of 2 is not needed and the term is negated. This term for a
  // Bezier curve is usually 2(p1-p0).
  const DPoint B = {p0.x - p1.x, p0.y - p1.y};
  const DPoint C = p0;

  return Intersect(A.x, B.x, C.x, A.y, B.y, C.y, y_intercept, intersection_storage);
}

std::span<const float> BezierQuad::Intersect(double AX, double BX, double CX,
                                             double AY, double BY, double CY,
                                             double y_intercept,
                                             float intersection_storage[2]) {
  auto [discriminant, r0, r1] = Quads::Roots(AY, BY, CY - y_intercept);

  int intersection_count = 0;
  // Round the roots to the nearest float to generate the values t. Valid t's
  // are on the domain [0, 1].
  const double t0 = PinTRange(r0);
  if (0 <= t0 && t0 <= 1) {
    intersection_storage[intersection_count++] = static_cast<float>(Quads::EvalAt(AX, -2 * BX, CX, t0));
  }

  const double t1 = PinTRange(r1);
  if (0 <= t1 && t1 <= 1 && t1 != t0) {
    intersection_storage[intersection_count++] = static_cast<float>(Quads::EvalAt(AX, -2 * BX, CX, t1));
  }

  return {intersection_storage, static_cast<std::size_t>(intersection_count)};
}

} // namespace bkit
