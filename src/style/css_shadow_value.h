// Ported from: blink/renderer/core/css/css_shadow_value.h
// Copyright (C) 2004, 2005, 2006, 2008 Apple Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include <memory>
#include <utility>

#include "base/memory/values_equivalent.h"
#include "style/css_identifier_value.h"
#include "style/css_primitive_value.h"

namespace bkfont {

// One shadow of text-shadow. `blur`, `spread`, `style` and `color` may be
// null when omitted.
class CSSShadowValue : public CSSValue {
public:
  static std::shared_ptr<const CSSShadowValue> Create(std::shared_ptr<const CSSPrimitiveValue> x,
                                                      std::shared_ptr<const CSSPrimitiveValue> y,
                                                      std::shared_ptr<const CSSPrimitiveValue> blur,
                                                      std::shared_ptr<const CSSPrimitiveValue> spread,
                                                      std::shared_ptr<const CSSIdentifierValue> style,
                                                      std::shared_ptr<const CSSValue> color) {
    return std::make_shared<const CSSShadowValue>(std::move(x), std::move(y), std::move(blur), std::move(spread),
                                                  std::move(style), std::move(color));
  }
  CSSShadowValue(std::shared_ptr<const CSSPrimitiveValue> x, std::shared_ptr<const CSSPrimitiveValue> y,
                 std::shared_ptr<const CSSPrimitiveValue> blur, std::shared_ptr<const CSSPrimitiveValue> spread,
                 std::shared_ptr<const CSSIdentifierValue> style, std::shared_ptr<const CSSValue> color)
      : CSSValue(kShadowClass), x(std::move(x)), y(std::move(y)), blur(std::move(blur)), spread(std::move(spread)),
        style(std::move(style)), color(std::move(color)) {}

  bool Equals(const CSSShadowValue& o) const {
    return base::ValuesEquivalent(color, o.color) && base::ValuesEquivalent(x, o.x) &&
           base::ValuesEquivalent(y, o.y) && base::ValuesEquivalent(blur, o.blur) &&
           base::ValuesEquivalent(spread, o.spread) && base::ValuesEquivalent(style, o.style);
  }

  std::shared_ptr<const CSSPrimitiveValue> x;
  std::shared_ptr<const CSSPrimitiveValue> y;
  std::shared_ptr<const CSSPrimitiveValue> blur;
  std::shared_ptr<const CSSPrimitiveValue> spread;
  std::shared_ptr<const CSSIdentifierValue> style;
  std::shared_ptr<const CSSValue> color;
};

template <>
struct DowncastTraits<CSSShadowValue> {
  static bool AllowFrom(const CSSValue& value) { return value.IsShadowValue(); }
};

} // namespace bkfont
