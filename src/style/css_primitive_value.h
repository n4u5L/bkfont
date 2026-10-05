// Ported from: blink/renderer/core/css/css_primitive_value.h
// Copyright (C) 2004, 2005, 2006, 2007, 2008 Apple Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include <cmath>
#include <limits>

#include "geometry/length.h"
#include "style/css_value.h"

namespace bkfont {

// Dimension calculations are imprecise, often resulting in values of e.g.
// 44.99998. We need to go ahead and round if we're really close to the next
// integer value.
template <typename T>
inline T RoundForImpreciseConversion(double value) {
  value += (value < 0) ? -0.01 : +0.01;
  return ((value > std::numeric_limits<T>::max()) || (value < std::numeric_limits<T>::min()))
             ? 0
             : static_cast<T>(value);
}

template <>
inline float RoundForImpreciseConversion(double value) {
  double ceiled_value = std::ceil(value);
  double proximity_to_next_int = ceiled_value - value;
  if (proximity_to_next_int <= 0.01 && value > 0) return static_cast<float>(ceiled_value);
  if (proximity_to_next_int >= 0.99 && value < 0) return static_cast<float>(std::floor(value));
  return static_cast<float>(value);
}

class CSSToLengthConversionData;

// Base of CSSNumericLiteralValue and CSSMathFunctionValue.
class CSSPrimitiveValue : public CSSValue {
public:
  // Subset of UnitType, in upstream order. There are no viewport, container,
  // time, resolution or flex units: the local model has no viewport,
  // containers or animations.
  enum class UnitType {
    kUnknown,
    kNumber,
    kPercentage,
    // Length units
    kEms,
    kExs,
    kPixels,
    kCentimeters,
    kMillimeters,
    kInches,
    kPoints,
    kPicas,
    kQuarterMillimeters,
    kRems,
    kRexs,
    kRchs,
    kRics,
    kChs,
    kIcs,
    kLhs,
    kRlhs,
    kCaps,
    kRcaps,
    // Angle units
    kDegrees,
    kRadians,
    kGradians,
    kTurns,
    // Other units
    kInteger,
  };

  enum UnitCategory {
    kUNumber,
    kUPercent,
    kULength,
    kUAngle,
    kUOther,
  };
  static UnitCategory UnitTypeToUnitCategory(UnitType);

  // Upstream CSSPrimitiveValue::ValueRange: the range the parser permits for
  // a calc() result in the target property.
  enum class ValueRange {
    kAll,
    kNonNegative,
    kInteger,
    kNonNegativeInteger,
    kPositiveInteger,
  };
  static Length::ValueRange ConversionToLengthValueRange(ValueRange);

  static bool IsAngle(UnitType unit) {
    return unit == UnitType::kDegrees || unit == UnitType::kRadians || unit == UnitType::kGradians ||
           unit == UnitType::kTurns;
  }
  static bool IsLength(UnitType type) {
    return (type >= UnitType::kEms && type <= UnitType::kRcaps);
  }
  static bool IsRelativeUnit(UnitType type) {
    return type == UnitType::kPercentage || type == UnitType::kEms || type == UnitType::kExs ||
           type == UnitType::kRems || type == UnitType::kChs || type == UnitType::kIcs || type == UnitType::kLhs ||
           type == UnitType::kCaps || type == UnitType::kRcaps || type == UnitType::kRexs ||
           type == UnitType::kRchs || type == UnitType::kRics || type == UnitType::kRlhs;
  }
  static bool IsFontRelativeLength(UnitType type) {
    return type == UnitType::kEms || type == UnitType::kExs || type == UnitType::kRems ||
           type == UnitType::kChs || type == UnitType::kCaps || type == UnitType::kRcaps ||
           type == UnitType::kIcs || type == UnitType::kLhs || type == UnitType::kRexs ||
           type == UnitType::kRchs || type == UnitType::kRics || type == UnitType::kRlhs;
  }
  // Canonical unit conversion factor for absolute units (px, deg).
  static double ConversionToCanonicalUnitsScaleFactor(UnitType);

  bool IsCalculated() const { return IsMathFunctionValue(); }
  bool IsNumber() const;
  bool IsInteger() const;
  bool IsPercentage() const;
  bool IsLength() const;
  bool IsAngle() const;
  // A calc() with both lengths and percentages.
  bool IsCalculatedPercentageWithLength() const;
  // Lengths that do not depend on a percentage basis.
  bool IsResolvableLength() const { return IsLength() && !IsCalculatedPercentageWithLength(); }

  // Computed values. The CSSToLengthConversionData supplies the font sizes,
  // line heights and zoom; percentages are never resolved against it.
  double ComputeDegrees(const CSSToLengthConversionData&) const;
  int ComputeInteger(const CSSToLengthConversionData&) const;
  // Percentages are divided by 100.
  double ComputeNumber(const CSSToLengthConversionData&) const;
  double ComputePercentage(const CSSToLengthConversionData&) const;
  // ComputeLength<float>() and ComputeLength<double>().
  float ComputeLength(const CSSToLengthConversionData&) const;
  double ComputeLengthDouble(const CSSToLengthConversionData&) const;
  // ComputeLength<Length>() for a resolvable length; percentages and
  // length-percentage calc() become percent or calculated Lengths.
  Length ConvertToLength(const CSSToLengthConversionData&) const;

protected:
  explicit CSSPrimitiveValue(ClassType class_type) : CSSValue(class_type) {}
};

template <>
struct DowncastTraits<CSSPrimitiveValue> {
  static bool AllowFrom(const CSSValue& value) { return value.IsPrimitiveValue(); }
};

} // namespace bkfont
