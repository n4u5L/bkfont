// Ported from: blink/renderer/core/css/css_primitive_value.cc
// Ported from: blink/renderer/core/css/css_numeric_literal_value.cc
// Ported from: blink/renderer/core/css/css_math_function_value.cc
// Ported from: blink/renderer/core/css/css_value_clamping_utils.cc
// Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009, 2010, 2012 Apple Inc.
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "css_primitive_value.h"

#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>

#include "base/math_extras.h"
#include "geometry/calculation_value.h"
#include "layout/layout_unit.h"
#include "style/css_math_function_value.h"
#include "style/css_numeric_literal_value.h"
#include "style/css_to_length_conversion_data.h"

namespace bkit {
namespace {

// CSSValueClampingUtils.
double ClampDouble(double value) {
  // https://www.w3.org/TR/css-values-4/#top-level-calculation
  if (std::isnan(value)) value = 0;
  return ClampTo<double>(value);
}
double ClampLength(double value) {
  return ClampDouble(value);
}
const int kMaxValueForCssLength = INT_MAX / LayoutUnit::kFixedPointDenominator - 2;
const int kMinValueForCssLength = INT_MIN / LayoutUnit::kFixedPointDenominator + 2;

// CSSPrimitiveValue::ClampToCSSLengthRange().
float ClampToCSSLengthRange(double value) {
  return ClampTo<float>(ClampLength(value), kMinValueForCssLength, kMaxValueForCssLength);
}

double ClampAngle(double value) {
  if (std::isnan(value)) value = 0;
  // The value must be multiple of 360deg.
  // Reference:  https://drafts.csswg.org/css-values/#numeric-types
  static constexpr double kApproxDoubleInfinityAngle = 2867080569122160;
  return ClampTo<double>(value, -kApproxDoubleInfinityAngle, kApproxDoubleInfinityAngle);
}

double DegreesOf(double value, CSSPrimitiveValue::UnitType unit) {
  using UnitType = CSSPrimitiveValue::UnitType;
  switch (unit) {
    case UnitType::kRadians: return Rad2deg(value);
    case UnitType::kGradians: return Grad2deg(value);
    case UnitType::kTurns: return Turn2deg(value);
    default: return value;
  }
}

} // namespace

CSSPrimitiveValue::UnitCategory CSSPrimitiveValue::UnitTypeToUnitCategory(UnitType type) {
  switch (type) {
    case UnitType::kNumber:
    case UnitType::kInteger: return kUNumber;
    case UnitType::kPercentage: return kUPercent;
    case UnitType::kDegrees:
    case UnitType::kRadians:
    case UnitType::kGradians:
    case UnitType::kTurns: return kUAngle;
    case UnitType::kUnknown: return kUOther;
    default: return IsLength(type) ? kULength : kUOther;
  }
}

Length::ValueRange CSSPrimitiveValue::ConversionToLengthValueRange(ValueRange range) {
  switch (range) {
    case ValueRange::kNonNegative: return Length::ValueRange::kNonNegative;
    case ValueRange::kAll: return Length::ValueRange::kAll;
    default: assert(false); return Length::ValueRange::kAll;
  }
}

double CSSPrimitiveValue::ConversionToCanonicalUnitsScaleFactor(UnitType unit_type) {
  switch (unit_type) {
    case UnitType::kRadians: return Rad2deg(1.0);
    case UnitType::kGradians: return Grad2deg(1.0);
    case UnitType::kTurns: return Turn2deg(1.0);
    default: return 1.0;
  }
}

bool CSSPrimitiveValue::IsNumber() const {
  if (IsNumericLiteralValue()) return To<CSSNumericLiteralValue>(this)->IsNumber();
  return To<CSSMathFunctionValue>(this)->GetCategory() == CSSMathFunctionValue::Category::kNumber;
}

bool CSSPrimitiveValue::IsInteger() const {
  if (IsNumericLiteralValue()) return To<CSSNumericLiteralValue>(this)->IsInteger();
  // calc() integers are numbers resolved in an integer range.
  const auto* calc = To<CSSMathFunctionValue>(this);
  return calc->GetCategory() == CSSMathFunctionValue::Category::kNumber &&
         calc->PermittedValueRange() >= ValueRange::kInteger;
}

bool CSSPrimitiveValue::IsPercentage() const {
  if (IsNumericLiteralValue()) return To<CSSNumericLiteralValue>(this)->IsPercentage();
  return To<CSSMathFunctionValue>(this)->GetCategory() == CSSMathFunctionValue::Category::kPercent;
}

bool CSSPrimitiveValue::IsLength() const {
  if (IsNumericLiteralValue()) return To<CSSNumericLiteralValue>(this)->IsLength();
  const auto category = To<CSSMathFunctionValue>(this)->GetCategory();
  return category == CSSMathFunctionValue::Category::kLength ||
         category == CSSMathFunctionValue::Category::kLengthPercent;
}

bool CSSPrimitiveValue::IsAngle() const {
  if (IsNumericLiteralValue()) return To<CSSNumericLiteralValue>(this)->IsAngle();
  return To<CSSMathFunctionValue>(this)->GetCategory() == CSSMathFunctionValue::Category::kAngle;
}

bool CSSPrimitiveValue::IsCalculatedPercentageWithLength() const {
  return IsCalculated() &&
         To<CSSMathFunctionValue>(this)->GetCategory() == CSSMathFunctionValue::Category::kLengthPercent;
}

double CSSPrimitiveValue::ComputeDegrees(const CSSToLengthConversionData& data) const {
  const double result = IsCalculated() ? To<CSSMathFunctionValue>(this)->ComputeDegrees(data)
                                       : To<CSSNumericLiteralValue>(this)->ComputeDegrees();
  return ClampAngle(result);
}

int CSSPrimitiveValue::ComputeInteger(const CSSToLengthConversionData& data) const {
  assert(IsNumber());
  return IsCalculated() ? To<CSSMathFunctionValue>(this)->ComputeInteger(data)
                        : To<CSSNumericLiteralValue>(this)->ComputeInteger();
}

double CSSPrimitiveValue::ComputeNumber(const CSSToLengthConversionData& data) const {
  assert(IsNumber() || IsPercentage());
  // NOTE: Division by 100 will be done by ComputeNumber() if needed.
  return IsCalculated() ? To<CSSMathFunctionValue>(this)->ComputeNumber(data)
                        : To<CSSNumericLiteralValue>(this)->ComputeNumber();
}

double CSSPrimitiveValue::ComputePercentage(const CSSToLengthConversionData& data) const {
  assert(IsPercentage());
  return IsCalculated() ? To<CSSMathFunctionValue>(this)->ComputePercentage(data)
                        : To<CSSNumericLiteralValue>(this)->ComputePercentage();
}

float CSSPrimitiveValue::ComputeLength(const CSSToLengthConversionData& data) const {
  return ClampTo<float>(ClampLength(ComputeLengthDouble(data)));
}

double CSSPrimitiveValue::ComputeLengthDouble(const CSSToLengthConversionData& data) const {
  if (IsCalculated()) return To<CSSMathFunctionValue>(this)->ComputeLengthPx(data);
  return To<CSSNumericLiteralValue>(this)->ComputeLengthPx(data);
}

Length CSSPrimitiveValue::ConvertToLength(const CSSToLengthConversionData& data) const {
  if (IsResolvableLength()) {
    // ComputeLength<Length>().
    return Length::Fixed(ClampToCSSLengthRange(ComputeLengthDouble(data)));
  }
  if (IsPercentage()) {
    if (IsNumericLiteralValue() || !To<CSSMathFunctionValue>(this)->AllowsNegativePercentageReference())
      return Length::Percent(ClampTo<float>(ClampLength(ComputePercentage(data))));
  }
  assert(IsCalculated());
  return To<CSSMathFunctionValue>(this)->ConvertToLength(data);
}

// CSSNumericLiteralValue

double CSSNumericLiteralValue::ComputeDegrees() const {
  assert(IsAngle());
  return DegreesOf(num_, type_);
}

double CSSNumericLiteralValue::ComputeLengthPx(const CSSToLengthConversionData& data) const {
  assert(IsLength());
  return data.ZoomedComputedPixels(num_, GetType());
}

int CSSNumericLiteralValue::ComputeInteger() const {
  assert(IsNumber());
  return ClampTo<int>(num_);
}

double CSSNumericLiteralValue::ComputeNumber() const {
  assert(IsNumber() || IsPercentage());
  if (IsPercentage()) return ClampTo<double>(num_ / 100.0);
  return ClampTo<double>(num_);
}

double CSSNumericLiteralValue::ComputePercentage() const {
  assert(IsPercentage());
  return ClampDouble(num_);
}

// CSSMathFunctionValue

std::optional<CSSMathFunctionValue::Category> CSSMathFunctionValue::CategoryOf(const Vector<Term>& terms) {
  if (terms.empty()) return std::nullopt;
  bool number = false, percent = false, length = false, angle = false;
  for (const Term& term : terms) {
    switch (UnitTypeToUnitCategory(term.unit)) {
      case kUNumber: number = true; break;
      case kUPercent: percent = true; break;
      case kULength: length = true; break;
      case kUAngle: angle = true; break;
      case kUOther: return std::nullopt;
    }
  }
  if (number) return percent || length || angle ? std::nullopt : std::optional(Category::kNumber);
  if (angle) return percent || length ? std::nullopt : std::optional(Category::kAngle);
  if (length) return percent ? Category::kLengthPercent : Category::kLength;
  return Category::kPercent;
}

double CSSMathFunctionValue::ClampToPermittedRange(double value) const {
  switch (PermittedValueRange()) {
    case ValueRange::kInteger: return RoundHalfTowardsPositiveInfinity(value);
    case ValueRange::kNonNegativeInteger: return RoundHalfTowardsPositiveInfinity(std::max(value, 0.0));
    case ValueRange::kPositiveInteger: return RoundHalfTowardsPositiveInfinity(std::max(value, 1.0));
    case ValueRange::kNonNegative: return std::max(value, 0.0);
    case ValueRange::kAll: return value;
  }
  return value;
}

double CSSMathFunctionValue::SumLengthPx(const CSSToLengthConversionData& data) const {
  double result = 0;
  for (const Term& term : terms_)
    if (term.unit != UnitType::kPercentage) result += data.ZoomedComputedPixels(term.value, term.unit);
  return result;
}

double CSSMathFunctionValue::SumPercent() const {
  double result = 0;
  for (const Term& term : terms_)
    if (term.unit == UnitType::kPercentage) result += term.value;
  return result;
}

double CSSMathFunctionValue::SumRaw() const {
  double result = 0;
  for (const Term& term : terms_) result += term.value * ConversionToCanonicalUnitsScaleFactor(term.unit);
  return result;
}

double CSSMathFunctionValue::ComputeDegrees(const CSSToLengthConversionData&) const {
  assert(GetCategory() == Category::kAngle);
  return ClampToPermittedRange(SumRaw());
}

double CSSMathFunctionValue::ComputeLengthPx(const CSSToLengthConversionData& data) const {
  // |CSSToLengthConversionData| only resolves relative length units, but not
  // percentages.
  assert(GetCategory() == Category::kLength);
  return ClampToPermittedRange(SumLengthPx(data));
}

int CSSMathFunctionValue::ComputeInteger(const CSSToLengthConversionData&) const {
  assert(GetCategory() == Category::kNumber);
  return ClampToWithNaNTo0<int>(ClampToPermittedRange(SumRaw()));
}

double CSSMathFunctionValue::ComputeNumber(const CSSToLengthConversionData&) const {
  assert(GetCategory() == Category::kNumber || GetCategory() == Category::kPercent);
  double value = ClampToPermittedRange(SumRaw());
  if (GetCategory() == Category::kPercent) value /= 100.0;
  return value;
}

double CSSMathFunctionValue::ComputePercentage(const CSSToLengthConversionData&) const {
  assert(GetCategory() == Category::kPercent);
  return ClampDouble(ClampToPermittedRange(SumRaw()));
}

Length CSSMathFunctionValue::ConvertToLength(const CSSToLengthConversionData& data) const {
  if (IsResolvableLength()) return Length::Fixed(ClampTo<float>(ComputeLengthPx(data)));
  // ToCalcValue(): a pixels-and-percent sum, clamped by the target range at
  // use time.
  const PixelsAndPercent pixels_and_percent(ClampTo<float>(SumLengthPx(data)), ClampTo<float>(SumPercent()),
                                            /*has_explicit_pixels=*/true, /*has_explicit_percent=*/true);
  return Length(std::make_shared<const CalculationValue>(pixels_and_percent,
                                                         ConversionToLengthValueRange(PermittedValueRange())));
}

} // namespace bkit
