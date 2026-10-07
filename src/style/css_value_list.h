// Ported from: blink/renderer/core/css/css_value_list.h
// Ported from: blink/renderer/core/css/css_function_value.h
// Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2010 Apple Inc.
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include <utility>

#include "base/memory/values_equivalent.h"
#include "base/vector.h"
#include "style/css_value.h"
#include "style/css_value_keywords.h"

namespace bkfont {

class CSSValueList : public CSSValue {
public:
  using Values = Vector<scoped_refptr<const CSSValue>>;
  using const_iterator = Values::const_iterator;

  static scoped_refptr<const CSSValueList> CreateCommaSeparated(Values values) {
    return base::AdoptRef(new CSSValueList(kCommaSeparator, std::move(values)));
  }
  static scoped_refptr<const CSSValueList> CreateSpaceSeparated(Values values) {
    return base::AdoptRef(new CSSValueList(kSpaceSeparator, std::move(values)));
  }
  static scoped_refptr<const CSSValueList> CreateSlashSeparated(Values values) {
    return base::AdoptRef(new CSSValueList(kSlashSeparator, std::move(values)));
  }

  CSSValueList(ValueListSeparator separator, Values values)
      : CSSValueList(kValueListClass, separator, std::move(values)) {}

  const_iterator begin() const { return values_.begin(); }
  const_iterator end() const { return values_.end(); }
  wtf_size_t length() const { return values_.size(); }
  const CSSValue& Item(wtf_size_t index) const { return *values_[index]; }
  const CSSValue& First() const { return *values_.front(); }
  const CSSValue& Last() const { return *values_.back(); }
  bool IsCommaSeparated() const { return value_list_separator_ == kCommaSeparator; }
  bool IsSpaceSeparated() const { return value_list_separator_ == kSpaceSeparator; }
  bool IsSlashSeparated() const { return value_list_separator_ == kSlashSeparator; }
  bool HasValue(const CSSValue& value) const {
    for (const auto& item : values_)
      if (*item == value) return true;
    return false;
  }

  bool Equals(const CSSValueList& other) const {
    if (value_list_separator_ != other.value_list_separator_ || values_.size() != other.values_.size()) return false;
    for (wtf_size_t i = 0; i < values_.size(); ++i)
      if (!base::ValuesEquivalent(values_[i], other.values_[i])) return false;
    return true;
  }

protected:
  CSSValueList(ClassType class_type, ValueListSeparator separator, Values values)
      : CSSValue(class_type), values_(std::move(values)) {
    value_list_separator_ = separator;
  }

private:
  Values values_;
};

template <>
struct DowncastTraits<CSSValueList> {
  static bool AllowFrom(const CSSValue& value) { return value.IsValueList(); }
};

// A comma-separated function value, such as stylistic(alias).
class CSSFunctionValue : public CSSValueList {
public:
  static scoped_refptr<const CSSFunctionValue> Create(CSSValueID id, Values values) {
    return base::AdoptRef(new CSSFunctionValue(id, std::move(values)));
  }
  CSSFunctionValue(CSSValueID id, Values values)
      : CSSValueList(kFunctionClass, kCommaSeparator, std::move(values)), value_id_(id) {}

  CSSValueID FunctionType() const { return value_id_; }

  bool Equals(const CSSFunctionValue& other) const {
    return value_id_ == other.value_id_ && CSSValueList::Equals(other);
  }

private:
  CSSValueID value_id_;
};

template <>
struct DowncastTraits<CSSFunctionValue> {
  static bool AllowFrom(const CSSValue& value) { return value.IsFunctionValue(); }
};

} // namespace bkfont
