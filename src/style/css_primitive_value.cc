// Ported from: blink/renderer/core/css/css_primitive_value.cc
// Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009, 2010, 2012 Apple Inc.
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "css_primitive_value.h"

#include "style/css_numeric_literal_value.h"

namespace bkfont {

// The local CSSMathFunctionValue is always a length-percentage sum, so it is
// never a number or a percentage, and is a length (category kLength or
// kLengthPercent) as upstream reports for such calc() values.
bool CSSPrimitiveValue::IsNumber() const {
  if (IsNumericLiteralValue()) return To<CSSNumericLiteralValue>(this)->IsNumber();
  return false;
}

bool CSSPrimitiveValue::IsPercentage() const {
  if (IsNumericLiteralValue()) return To<CSSNumericLiteralValue>(this)->IsPercentage();
  return false;
}

bool CSSPrimitiveValue::IsLength() const {
  if (IsNumericLiteralValue()) return To<CSSNumericLiteralValue>(this)->IsLength();
  return true;
}

} // namespace bkfont
