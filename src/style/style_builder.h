// Subset of blink/renderer/core/css/resolver/style_builder.h. Applies one
// cascaded longhand value: CSS-wide keywords as in ApplyPhysicalProperty(),
// other values as the longhand's ApplyValue() with StyleBuilderConverter.
#pragma once

#include "style/css_value.h"
#include "style/css_property_names.h"

namespace bkfont {

class StyleResolverState;

class StyleBuilder {
public:
  static void ApplyPhysicalProperty(CSSPropertyID, StyleResolverState&, const CSSValue&);
};

} // namespace bkfont
