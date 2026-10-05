// Ported from: blink/renderer/core/css/css_string_value.h
// Ported from: blink/renderer/core/css/css_custom_ident_value.h
// Ported from: blink/renderer/core/css/css_font_family_value.h
// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include <memory>

#include "base/text/atomic_string.h"
#include "base/text/wtf_string.h"
#include "style/css_value.h"

namespace bkfont {

// A <string>.
class CSSStringValue : public CSSValue {
public:
  static std::shared_ptr<const CSSStringValue> Create(const String& str) {
    return std::make_shared<const CSSStringValue>(str);
  }
  explicit CSSStringValue(const String& str) : CSSValue(kStringClass), string_(str) {}

  const String& Value() const { return string_; }

  bool Equals(const CSSStringValue& other) const { return string_ == other.string_; }

private:
  String string_;
};

template <>
struct DowncastTraits<CSSStringValue> {
  static bool AllowFrom(const CSSValue& value) { return value.IsStringValue(); }
};

// A <custom-ident> (or <dashed-ident>). There are no tree scopes.
class CSSCustomIdentValue : public CSSValue {
public:
  static std::shared_ptr<const CSSCustomIdentValue> Create(const AtomicString& str) {
    return std::make_shared<const CSSCustomIdentValue>(str);
  }
  explicit CSSCustomIdentValue(const AtomicString& str) : CSSValue(kCustomIdentClass), string_(str) {}

  const AtomicString& Value() const { return string_; }

  bool Equals(const CSSCustomIdentValue& other) const { return string_ == other.string_; }

private:
  AtomicString string_;
};

template <>
struct DowncastTraits<CSSCustomIdentValue> {
  static bool AllowFrom(const CSSValue& value) { return value.IsCustomIdentValue(); }
};

// A non-generic <family-name>, from a string or a sequence of identifiers.
class CSSFontFamilyValue : public CSSValue {
public:
  static std::shared_ptr<const CSSFontFamilyValue> Create(const AtomicString& family_name) {
    return std::make_shared<const CSSFontFamilyValue>(family_name);
  }
  explicit CSSFontFamilyValue(const AtomicString& str) : CSSValue(kFontFamilyClass), string_(str) {}

  const AtomicString& Value() const { return string_; }

  bool Equals(const CSSFontFamilyValue& other) const { return string_ == other.string_; }

private:
  AtomicString string_;
};

template <>
struct DowncastTraits<CSSFontFamilyValue> {
  static bool AllowFrom(const CSSValue& value) { return value.IsFontFamilyValue(); }
};

} // namespace bkfont
