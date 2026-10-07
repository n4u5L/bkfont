// Ported from: blink/renderer/core/css/css_initial_value.h
// Copyright (C) 2002 Lars Knoll (knoll@kde.org)
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include "style/css_value.h"

namespace bkfont {

class CSSInitialValue : public CSSValue {
public:
  // CssValuePool's shared instance.
  static scoped_refptr<const CSSInitialValue> Create() {
    static const auto value = base::AdoptRef(new CSSInitialValue());
    return value;
  }

  CSSInitialValue()
      : CSSValue(kInitialClass) {
  }

  bool Equals(const CSSInitialValue&) const {
    return true;
  }
};

template <>
struct DowncastTraits<CSSInitialValue> {
  static bool AllowFrom(const CSSValue& value) {
    return value.IsInitialValue();
  }
};

} // namespace bkfont
