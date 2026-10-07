// Ported from: blink/renderer/core/css/css_identifier_value.h
// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include "style/css_value.h"
#include "style/css_value_keywords.h"

namespace bkfont {

// css_value_id_mappings.h. Include it where ConvertTo() is instantiated.
template <class T>
T CssValueIDToPlatformEnum(CSSValueID);

class CSSIdentifierValue : public CSSValue {
public:
  static scoped_refptr<const CSSIdentifierValue> Create(CSSValueID value_id) {
    return base::AdoptRef(new CSSIdentifierValue(value_id));
  }

  explicit CSSIdentifierValue(CSSValueID value_id) : CSSValue(kIdentifierClass), value_id_(value_id) {}

  CSSValueID GetValueID() const { return value_id_; }

  template <typename T>
  T ConvertTo() const { // Overridden for special cases in css_value_id_mappings.h
    return CssValueIDToPlatformEnum<T>(value_id_);
  }

  bool Equals(const CSSIdentifierValue& other) const { return value_id_ == other.value_id_; }

private:
  CSSValueID value_id_;
};

template <>
struct DowncastTraits<CSSIdentifierValue> {
  static bool AllowFrom(const CSSValue& value) { return value.IsIdentifierValue(); }
};

} // namespace bkfont
