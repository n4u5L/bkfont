// The grammar checks of the ported longhands, in place of their
// ParseSingleValue() (core/css/properties/longhands/longhands_custom.cc,
// css_parsing_utils.cc and CSSParserFastPaths::IsValidKeywordPropertyAndValue)
// for values that are built directly instead of parsed.
#pragma once

#include <memory>

#include "style/css_primitive_value.h"
#include "style/css_property_names.h"
#include "style/css_value.h"

namespace bkfont {

// Whether `value` is a value the property's parser can produce. CSS-wide
// keywords are valid for every longhand.
bool IsValidLonghandValue(CSSPropertyID, const CSSValue&);

// The value with every calc() given the range the parser records for its
// position in the property (CSSMathFunctionValue::PermittedValueRange()).
// `value` must be valid.
std::shared_ptr<const CSSValue> WithParsedCalcRanges(CSSPropertyID, std::shared_ptr<const CSSValue> value);

} // namespace bkfont
