// Ported from: skia/src/base/SkBezierCurves.h
// Ported from: skia/src/base/SkQuads.h
// Ported from: skia/src/base/SkCubics.h

#pragma once

#include <span>

#include "rect.h"

namespace bkit {

// SkQuads. Solves quadratics written as A*x^2 - 2*B*x + C (Roots) or
// A*x^2 + B*x + C (RootsReal).
class Quads {
public:
  // Calculate a very accurate discriminant. Given
  //    A*t^2 -2*B*t + C = 0,
  // calculate
  //    B^2 - AC
  // accurate to 2 bits.
  static double Discriminant(double A, double B, double C);

  struct RootResult {
    double discriminant;
    double root0;
    double root1;
  };

  // Calculate the roots of a quadratic. Given
  //    A*t^2 -2*B*t + C = 0,
  // calculate the roots.
  //
  // This does not try to detect a linear configuration of the equation, or
  // detect if the two roots are the same. It returns the discriminant and the
  // two roots.
  //
  // Not this uses a different form the quadratic equation to reduce rounding
  // error. Give standard A, B, C. You can call this root finder with:
  //    Roots(A, -0.5*B, C)
  // to find the roots of A*x^2 + B*x + C.
  //
  // Returns {discriminant, r0, r1}.
  static RootResult Roots(double A, double B, double C);

  // Puts up to 2 real solutions to the equation
  //   A*t^2 + B*t + C = 0
  // in the provided array.
  static int RootsReal(double A, double B, double C, double solution[2]);

  // Evaluates the quadratic function with the 3 provided coefficients and the
  // provided variable.
  static double EvalAt(double A, double B, double C, double t);
};

// SkCubics.
class Cubics {
public:
  // Puts up to 3 real solutions to the equation
  //   A*t^3 + B*t^2 + C*t + d = 0
  // in the provided array and returns how many roots that was.
  static int RootsReal(double A, double B, double C, double D, double solution[3]);

  // Evaluates the cubic function with the 4 provided coefficients and the
  // provided variable.
  static double EvalAt(double A, double B, double C, double D, double t);
};

// SkBezierQuad.
class BezierQuad {
public:
  // Return a span containing the x values for the intersection of the
  // quadratic Bezier curve with the horizontal line y = yIntercept. The
  // storage must hold two values.
  static std::span<const float> IntersectWithHorizontalLine(std::span<const ScalarPoint> control_points, float y_intercept,
                                                            float intersection_storage[2]);

  // Given AY*t^2 -2*BY*t + CY = 0 and AX*t^2 - 2*BX*t + CX = 0,
  //
  // Find the t where AY*t^2 - 2*BY*t + CY - y = 0, then return AX*t^2 + -
  // 2*BX*t + CX where t is on [0, 1].
  //
  // - y - the value of the y-axis line to intersect with the quadratic.
  static std::span<const float> Intersect(double AX, double BX, double CX,
                                          double AY, double BY, double CY,
                                          double y_intercept,
                                          float intersection_storage[2]);
};

// SkBezierCubic.
class BezierCubic {
public:
  // Return a span containing the x values for the intersection of the cubic
  // Bezier curve with the horizontal line y = yIntercept. The storage must
  // hold three values.
  static std::span<const float> IntersectWithHorizontalLine(std::span<const ScalarPoint> control_points, float y_intercept,
                                                            float intersection_storage[3]);

  // Given the cubic coefficients (from the power basis) for x and y, find the
  // x values where the curve crosses y = to_intersect for t on [0, 1].
  static std::span<const float> Intersect(double AX, double BX, double CX, double DX,
                                          double AY, double BY, double CY, double DY,
                                          float to_intersect, float intersections_storage[3]);
};

// sk_double_nearly_zero.
bool DoubleNearlyZero(double a);

// sk_doubles_nearly_equal_ulps.
bool DoublesNearlyEqualUlps(double a, double b, unsigned max_ulps_diff = 16);

} // namespace bkit
