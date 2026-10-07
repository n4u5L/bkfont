// Adapted from: blink/renderer/core/css/css_math_function_value.h
// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
//
// Local subset: a linear sum of terms with units, kept until computed-value
// conversion. It replaces CSSMathExpressionNode and has no min()/max()/
// clamp(), products, variables or environment values. All terms belong to
// one category: numbers, percentages, angles, or lengths optionally mixed
// with percentages.
#pragma once

#include <optional>
#include <utility>

#include "base/vector.h"
#include "style/css_primitive_value.h"

namespace bkfont {

class CSSMathFunctionValue : public CSSPrimitiveValue {
public:
  struct Term {
    double value;
    UnitType unit;
    bool operator==(const Term&) const = default;
  };

  // CalculationResultCategory of the sum.
  enum class Category {
    kNumber,
    kPercent,
    kLength,
    kLengthPercent,
    kAngle,
  };
  // The category of `terms`, or nullopt when empty or mixed.
  static std::optional<Category> CategoryOf(const Vector<Term>& terms);

  // The parser records the range the target property permits; computed
  // values are clamped to it. The properties' Make() functions pass it.
  static scoped_refptr<const CSSMathFunctionValue> Create(Vector<Term> terms, ValueRange range = ValueRange::kAll) {
    return base::AdoptRef(new CSSMathFunctionValue(std::move(terms), range));
  }

  CSSMathFunctionValue(Vector<Term> terms, ValueRange range)
      : CSSPrimitiveValue(kMathFunctionClass), terms_(std::move(terms)), value_range_in_target_context_(range) {}

  const Vector<Term>& Terms() const { return terms_; }
  ValueRange PermittedValueRange() const { return value_range_in_target_context_; }
  // Callers check CategoryOf() first; an invalid sum reports kNumber.
  Category GetCategory() const { return CategoryOf(terms_).value_or(Category::kNumber); }
  bool AllowsNegativePercentageReference() const { return false; }

  double ComputeDegrees(const CSSToLengthConversionData&) const;
  double ComputeLengthPx(const CSSToLengthConversionData&) const;
  int ComputeInteger(const CSSToLengthConversionData&) const;
  double ComputeNumber(const CSSToLengthConversionData&) const;
  double ComputePercentage(const CSSToLengthConversionData&) const;
  Length ConvertToLength(const CSSToLengthConversionData&) const;

  bool Equals(const CSSMathFunctionValue& other) const {
    return terms_ == other.terms_ && value_range_in_target_context_ == other.value_range_in_target_context_;
  }

private:
  double ClampToPermittedRange(double) const;
  // The sum of the non-percentage terms in zoomed pixels, and the sum of the
  // percentage terms.
  double SumLengthPx(const CSSToLengthConversionData&) const;
  double SumPercent() const;
  double SumRaw() const;

  Vector<Term> terms_;
  ValueRange value_range_in_target_context_;
};

template <>
struct DowncastTraits<CSSMathFunctionValue> {
  static bool AllowFrom(const CSSValue& value) { return value.IsMathFunctionValue(); }
};

} // namespace bkfont
