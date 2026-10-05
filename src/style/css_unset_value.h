// Ported from: blink/renderer/core/css/css_unset_value.h
// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include <memory>

#include "style/css_value.h"

namespace bkfont {
namespace cssvalue {

class CSSUnsetValue : public CSSValue {
public:
  // CssValuePool's shared instance.
  static std::shared_ptr<const CSSUnsetValue> Create() {
    static const auto value = std::make_shared<const CSSUnsetValue>();
    return value;
  }

  CSSUnsetValue()
      : CSSValue(kUnsetClass) {
  }

  bool Equals(const CSSUnsetValue&) const {
    return true;
  }
};

} // namespace cssvalue

template <>
struct DowncastTraits<cssvalue::CSSUnsetValue> {
  static bool AllowFrom(const CSSValue& value) {
    return value.IsUnsetValue();
  }
};

} // namespace bkfont
