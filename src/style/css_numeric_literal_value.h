// Ported from: blink/renderer/core/css/css_numeric_literal_value.h
// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include <memory>

#include "style/css_primitive_value.h"

namespace bkfont {

// Represents a numeric literal: a number, percentage or length with a unit.
class CSSNumericLiteralValue : public CSSPrimitiveValue {
public:
  static std::shared_ptr<const CSSNumericLiteralValue> Create(double num, UnitType type) {
    return std::make_shared<const CSSNumericLiteralValue>(num, type);
  }

  CSSNumericLiteralValue(double num, UnitType type)
      : CSSPrimitiveValue(kNumericLiteralClass),
        num_(num),
        type_(type) {
  }

  UnitType GetType() const {
    return type_;
  }
  bool IsNumber() const {
    return type_ == UnitType::kNumber;
  }
  bool IsPercentage() const {
    return type_ == UnitType::kPercentage;
  }
  bool IsLength() const {
    return CSSPrimitiveValue::IsLength(type_);
  }
  double DoubleValue() const {
    return num_;
  }

  bool Equals(const CSSNumericLiteralValue& other) const {
    return type_ == other.type_ && num_ == other.num_;
  }

private:
  double num_;
  UnitType type_;
};

template <>
struct DowncastTraits<CSSNumericLiteralValue> {
  static bool AllowFrom(const CSSValue& value) {
    return value.IsNumericLiteralValue();
  }
};

} // namespace bkfont
