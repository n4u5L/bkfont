// Ported from: blink/renderer/core/css/css_primitive_value.h
// Copyright (C) 2004, 2005, 2006, 2007, 2008 Apple Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include "style/css_value.h"

namespace bkfont {

// Base of CSSNumericLiteralValue and CSSMathFunctionValue.
class CSSPrimitiveValue : public CSSValue {
public:
  // Subset of UnitType, in upstream order.
  enum class UnitType {
    kUnknown,
    kNumber,
    kPercentage,
    kEms,
    kPixels,
    kRems,
    kLhs,
    kRlhs,
  };

  static bool IsLength(UnitType type) {
    return type == UnitType::kEms || type == UnitType::kPixels || type == UnitType::kRems ||
           type == UnitType::kLhs || type == UnitType::kRlhs;
  }

  bool IsCalculated() const {
    return IsMathFunctionValue();
  }
  bool IsNumber() const;
  bool IsPercentage() const;
  bool IsLength() const;

protected:
  explicit CSSPrimitiveValue(ClassType class_type)
      : CSSValue(class_type) {
  }
};

template <>
struct DowncastTraits<CSSPrimitiveValue> {
  static bool AllowFrom(const CSSValue& value) {
    return value.IsPrimitiveValue();
  }
};

} // namespace bkfont
