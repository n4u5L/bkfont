// Ported from: blink/renderer/core/css/css_inherited_value.h
// Copyright (C) 2002 Lars Knoll (knoll@kde.org)
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include "style/css_value.h"

namespace bkit {

class CSSInheritedValue : public CSSValue {
public:
  // CssValuePool's shared instance.
  static scoped_refptr<const CSSInheritedValue> Create() {
    static const auto value = base::AdoptRef(new CSSInheritedValue());
    return value;
  }

  CSSInheritedValue()
      : CSSValue(kInheritedClass) {
  }

  bool Equals(const CSSInheritedValue&) const {
    return true;
  }
};

template <>
struct DowncastTraits<CSSInheritedValue> {
  static bool AllowFrom(const CSSValue& value) {
    return value.IsInheritedValue();
  }
};

} // namespace bkit
