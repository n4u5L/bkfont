// Ported from: blink/renderer/core/css/css_value_pair.h
// Copyright (C) 2006 Apple Computer, Inc.
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include <cassert>
#include <memory>
#include <utility>

#include "base/memory/values_equivalent.h"
#include "style/css_value.h"

namespace bkfont {

class CSSValuePair : public CSSValue {
public:
  enum IdenticalValuesPolicy { kDropIdenticalValues, kKeepIdenticalValues };

  static std::shared_ptr<const CSSValuePair> Create(std::shared_ptr<const CSSValue> first,
                                                    std::shared_ptr<const CSSValue> second,
                                                    IdenticalValuesPolicy policy) {
    return std::make_shared<const CSSValuePair>(std::move(first), std::move(second), policy);
  }

  CSSValuePair(std::shared_ptr<const CSSValue> first, std::shared_ptr<const CSSValue> second,
               IdenticalValuesPolicy identical_values_policy)
      : CSSValue(kValuePairClass), first_(std::move(first)), second_(std::move(second)),
        identical_values_policy_(identical_values_policy) {
    assert(first_);
    assert(second_);
  }

  const CSSValue& First() const { return *first_; }
  const CSSValue& Second() const { return *second_; }
  bool KeepIdenticalValues() const { return identical_values_policy_ == kKeepIdenticalValues; }

  bool Equals(const CSSValuePair& other) const {
    return base::ValuesEquivalent(first_, other.first_) && base::ValuesEquivalent(second_, other.second_) &&
           identical_values_policy_ == other.identical_values_policy_;
  }

private:
  std::shared_ptr<const CSSValue> first_;
  std::shared_ptr<const CSSValue> second_;
  IdenticalValuesPolicy identical_values_policy_;
};

template <>
struct DowncastTraits<CSSValuePair> {
  static bool AllowFrom(const CSSValue& value) { return value.IsValuePair(); }
};

} // namespace bkfont
