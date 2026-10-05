// Ported from: blink/renderer/core/css/css_identifier_value.h
// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include <memory>

#include "style/css_value.h"
#include "style/css_value_keywords.h"

namespace bkfont {

class CSSIdentifierValue : public CSSValue {
public:
  static std::shared_ptr<const CSSIdentifierValue> Create(CSSValueID value_id) {
    return std::make_shared<const CSSIdentifierValue>(value_id);
  }

  explicit CSSIdentifierValue(CSSValueID value_id)
      : CSSValue(kIdentifierClass),
        value_id_(value_id) {
  }

  CSSValueID GetValueID() const {
    return value_id_;
  }

  bool Equals(const CSSIdentifierValue& other) const {
    return value_id_ == other.value_id_;
  }

private:
  CSSValueID value_id_;
};

template <>
struct DowncastTraits<CSSIdentifierValue> {
  static bool AllowFrom(const CSSValue& value) {
    return value.IsIdentifierValue();
  }
};

} // namespace bkfont
