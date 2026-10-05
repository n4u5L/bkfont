// Ported from: blink/renderer/core/css/css_value.h
// Copyright (C) 2008 Apple Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license in LICENSE.
//
// Subset of the class types used by the ported longhands. Values are
// immutable and shared through std::shared_ptr<const CSSValue> instead of
// Oilpan; subclasses are created by their Create() functions.
#pragma once

#include <cstdint>
#include <memory>

#include "base/casting.h"

namespace bkfont {

class CSSValue {
public:
  bool IsNumericLiteralValue() const { return class_type_ == kNumericLiteralClass; }
  bool IsMathFunctionValue() const { return class_type_ == kMathFunctionClass; }
  bool IsPrimitiveValue() const {
    return class_type_ == kNumericLiteralClass || class_type_ == kMathFunctionClass;
  }
  bool IsIdentifierValue() const { return class_type_ == kIdentifierClass; }
  bool IsColorValue() const { return class_type_ == kColorClass; }
  bool IsCustomIdentValue() const { return class_type_ == kCustomIdentClass; }
  bool IsStringValue() const { return class_type_ == kStringClass; }
  bool IsValuePair() const { return class_type_ == kValuePairClass; }
  bool IsFontFeatureValue() const { return class_type_ == kFontFeatureClass; }
  bool IsFontFamilyValue() const { return class_type_ == kFontFamilyClass; }
  bool IsFontStyleRangeValue() const { return class_type_ == kFontStyleRangeClass; }
  bool IsFontVariationValue() const { return class_type_ == kFontVariationClass; }
  bool IsAlternateValue() const { return class_type_ == kAlternateClass; }
  bool IsInheritedValue() const { return class_type_ == kInheritedClass; }
  bool IsInitialValue() const { return class_type_ == kInitialClass; }
  bool IsUnsetValue() const { return class_type_ == kUnsetClass; }
  bool IsCSSWideKeyword() const { return class_type_ >= kInheritedClass && class_type_ <= kUnsetClass; }
  bool IsShadowValue() const { return class_type_ == kShadowClass; }
  bool IsPaletteMixValue() const { return class_type_ == kPaletteMixClass; }
  bool IsValueList() const { return class_type_ >= kValueListClass; }
  bool IsFunctionValue() const { return class_type_ == kFunctionClass; }

  bool operator==(const CSSValue&) const;

protected:
  // Upstream order, restricted to the ported classes.
  enum ClassType {
    kNumericLiteralClass,
    kMathFunctionClass,
    kIdentifierClass,
    kColorClass,
    kCustomIdentClass,
    kStringClass,
    kValuePairClass,

    kFontFeatureClass,
    kFontFamilyClass,
    kFontStyleRangeClass,
    kFontVariationClass,
    kAlternateClass,

    kInheritedClass,
    kInitialClass,
    kUnsetClass,

    kShadowClass,
    kPaletteMixClass,

    // List class types must appear after ValueListClass.
    kValueListClass,
    kFunctionClass,
  };

  static const size_t kValueListSeparatorBits = 2;
  enum ValueListSeparator { kSpaceSeparator, kCommaSeparator, kSlashSeparator };

  explicit CSSValue(ClassType class_type) : class_type_(class_type) {}
  ClassType GetClassType() const { return class_type_; }

  // CSSValueList and CSSValuePair data, kept here as upstream does to share
  // the padding of the base class.
  uint8_t value_list_separator_ = kSpaceSeparator;

private:
  const ClassType class_type_;
};

} // namespace bkfont
