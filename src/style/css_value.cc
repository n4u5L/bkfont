// Ported from: blink/renderer/core/css/css_value.cc
// Copyright (C) 2011 Andreas Kling (kling@webkit.org)
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "css_value.h"

#include "base/notreached.h"
#include "style/css_color.h"
#include "style/css_font_values.h"
#include "style/css_identifier_value.h"
#include "style/css_inherited_value.h"
#include "style/css_initial_value.h"
#include "style/css_math_function_value.h"
#include "style/css_numeric_literal_value.h"
#include "style/css_shadow_value.h"
#include "style/css_string_value.h"
#include "style/css_unset_value.h"
#include "style/css_value_list.h"
#include "style/css_value_pair.h"

namespace bkfont {
namespace {

template <class ChildClassType>
inline bool CompareCSSValues(const CSSValue& first, const CSSValue& second) {
  return static_cast<const ChildClassType&>(first).Equals(static_cast<const ChildClassType&>(second));
}

} // namespace

void CSSValueTraits::Destruct(const CSSValue* value) {
  value->Destroy();
}

void CSSValue::Destroy() const {
  switch (GetClassType()) {
    case kAlternateClass: delete static_cast<const cssvalue::CSSAlternateValue*>(this); return;
    case kColorClass: delete static_cast<const cssvalue::CSSColor*>(this); return;
    case kCustomIdentClass: delete static_cast<const CSSCustomIdentValue*>(this); return;
    case kFontFamilyClass: delete static_cast<const CSSFontFamilyValue*>(this); return;
    case kFontFeatureClass: delete static_cast<const cssvalue::CSSFontFeatureValue*>(this); return;
    case kFontStyleRangeClass: delete static_cast<const cssvalue::CSSFontStyleRangeValue*>(this); return;
    case kFontVariationClass: delete static_cast<const cssvalue::CSSFontVariationValue*>(this); return;
    case kFunctionClass: delete static_cast<const CSSFunctionValue*>(this); return;
    case kIdentifierClass: delete static_cast<const CSSIdentifierValue*>(this); return;
    case kInheritedClass: delete static_cast<const CSSInheritedValue*>(this); return;
    case kInitialClass: delete static_cast<const CSSInitialValue*>(this); return;
    case kMathFunctionClass: delete static_cast<const CSSMathFunctionValue*>(this); return;
    case kNumericLiteralClass: delete static_cast<const CSSNumericLiteralValue*>(this); return;
    case kPaletteMixClass: delete static_cast<const cssvalue::CSSPaletteMixValue*>(this); return;
    case kShadowClass: delete static_cast<const CSSShadowValue*>(this); return;
    case kStringClass: delete static_cast<const CSSStringValue*>(this); return;
    case kUnsetClass: delete static_cast<const cssvalue::CSSUnsetValue*>(this); return;
    case kValueListClass: delete static_cast<const CSSValueList*>(this); return;
    case kValuePairClass: delete static_cast<const CSSValuePair*>(this); return;
  }
  NOTREACHED();
}

bool CSSValue::operator==(const CSSValue& other) const {
  if (class_type_ == other.class_type_) {
    switch (GetClassType()) {
      case kAlternateClass: return CompareCSSValues<cssvalue::CSSAlternateValue>(*this, other);
      case kColorClass: return CompareCSSValues<cssvalue::CSSColor>(*this, other);
      case kCustomIdentClass: return CompareCSSValues<CSSCustomIdentValue>(*this, other);
      case kFontFamilyClass: return CompareCSSValues<CSSFontFamilyValue>(*this, other);
      case kFontFeatureClass: return CompareCSSValues<cssvalue::CSSFontFeatureValue>(*this, other);
      case kFontStyleRangeClass: return CompareCSSValues<cssvalue::CSSFontStyleRangeValue>(*this, other);
      case kFontVariationClass: return CompareCSSValues<cssvalue::CSSFontVariationValue>(*this, other);
      case kFunctionClass: return CompareCSSValues<CSSFunctionValue>(*this, other);
      case kIdentifierClass: return CompareCSSValues<CSSIdentifierValue>(*this, other);
      case kInheritedClass: return CompareCSSValues<CSSInheritedValue>(*this, other);
      case kInitialClass: return CompareCSSValues<CSSInitialValue>(*this, other);
      case kMathFunctionClass: return CompareCSSValues<CSSMathFunctionValue>(*this, other);
      case kNumericLiteralClass: return CompareCSSValues<CSSNumericLiteralValue>(*this, other);
      case kPaletteMixClass: return CompareCSSValues<cssvalue::CSSPaletteMixValue>(*this, other);
      case kShadowClass: return CompareCSSValues<CSSShadowValue>(*this, other);
      case kStringClass: return CompareCSSValues<CSSStringValue>(*this, other);
      case kUnsetClass: return CompareCSSValues<cssvalue::CSSUnsetValue>(*this, other);
      case kValueListClass: return CompareCSSValues<CSSValueList>(*this, other);
      case kValuePairClass: return CompareCSSValues<CSSValuePair>(*this, other);
    }
    NOTREACHED();
  }
  return false;
}

} // namespace bkfont
