// Adapted from: blink/renderer/core/css/css_math_function_value.h
// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
//
// Local subset: a linear sum of length and percentage terms, kept with their
// units until computed-value conversion. It replaces CSSMathExpressionNode
// and has no min()/max()/clamp(), variables or environment values.
#pragma once

#include <memory>
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

  static std::shared_ptr<const CSSMathFunctionValue> Create(Vector<Term> terms) {
    return std::make_shared<const CSSMathFunctionValue>(std::move(terms));
  }

  explicit CSSMathFunctionValue(Vector<Term> terms)
      : CSSPrimitiveValue(kMathFunctionClass),
        terms_(std::move(terms)) {
  }

  const Vector<Term>& Terms() const {
    return terms_;
  }

  bool Equals(const CSSMathFunctionValue& other) const {
    return terms_ == other.terms_;
  }

private:
  Vector<Term> terms_;
};

template <>
struct DowncastTraits<CSSMathFunctionValue> {
  static bool AllowFrom(const CSSValue& value) {
    return value.IsMathFunctionValue();
  }
};

} // namespace bkfont
