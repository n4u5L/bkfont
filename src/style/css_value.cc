// Ported from: blink/renderer/core/css/css_value.cc
// Copyright (C) 2011 Andreas Kling (kling@webkit.org)
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "css_value.h"

#include "base/notreached.h"
#include "style/css_color.h"
#include "style/css_identifier_value.h"
#include "style/css_inherited_value.h"
#include "style/css_initial_value.h"
#include "style/css_math_function_value.h"
#include "style/css_numeric_literal_value.h"
#include "style/css_unset_value.h"

namespace bkfont {
namespace {

template <class ChildClassType>
inline bool CompareCSSValues(const CSSValue& first, const CSSValue& second) {
  return static_cast<const ChildClassType&>(first).Equals(static_cast<const ChildClassType&>(second));
}

} // namespace

bool CSSValue::operator==(const CSSValue& other) const {
  if (class_type_ == other.class_type_) {
    switch (GetClassType()) {
      case kColorClass:
        return CompareCSSValues<cssvalue::CSSColor>(*this, other);
      case kIdentifierClass:
        return CompareCSSValues<CSSIdentifierValue>(*this, other);
      case kInheritedClass:
        return CompareCSSValues<CSSInheritedValue>(*this, other);
      case kInitialClass:
        return CompareCSSValues<CSSInitialValue>(*this, other);
      case kMathFunctionClass:
        return CompareCSSValues<CSSMathFunctionValue>(*this, other);
      case kNumericLiteralClass:
        return CompareCSSValues<CSSNumericLiteralValue>(*this, other);
      case kUnsetClass:
        return CompareCSSValues<cssvalue::CSSUnsetValue>(*this, other);
    }
    NOTREACHED();
  }
  return false;
}

} // namespace bkfont
