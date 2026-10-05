// Ported from: blink/renderer/core/css/css_value.h
// Copyright (C) 2008 Apple Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license in LICENSE.
//
// Subset of the class types used by the ported longhands. Values are
// immutable and shared through std::shared_ptr<const CSSValue> instead of
// Oilpan; subclasses are created by their Create() functions.
#pragma once

#include <memory>

#include "base/casting.h"

namespace bkfont {

class CSSValue {
public:
  bool IsNumericLiteralValue() const {
    return class_type_ == kNumericLiteralClass;
  }
  bool IsMathFunctionValue() const {
    return class_type_ == kMathFunctionClass;
  }
  bool IsPrimitiveValue() const {
    return class_type_ == kNumericLiteralClass || class_type_ == kMathFunctionClass;
  }
  bool IsIdentifierValue() const {
    return class_type_ == kIdentifierClass;
  }
  bool IsColorValue() const {
    return class_type_ == kColorClass;
  }
  bool IsInheritedValue() const {
    return class_type_ == kInheritedClass;
  }
  bool IsInitialValue() const {
    return class_type_ == kInitialClass;
  }
  bool IsUnsetValue() const {
    return class_type_ == kUnsetClass;
  }
  bool IsCSSWideKeyword() const {
    return class_type_ >= kInheritedClass && class_type_ <= kUnsetClass;
  }

  bool operator==(const CSSValue&) const;

protected:
  // Upstream order, restricted to the ported classes.
  enum ClassType {
    kNumericLiteralClass,
    kMathFunctionClass,
    kIdentifierClass,
    kColorClass,

    kInheritedClass,
    kInitialClass,
    kUnsetClass,
  };

  explicit CSSValue(ClassType class_type)
      : class_type_(class_type) {
  }
  ClassType GetClassType() const {
    return class_type_;
  }

private:
  const ClassType class_type_;
};

} // namespace bkfont
