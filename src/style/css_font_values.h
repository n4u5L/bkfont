// Ported from: blink/renderer/core/css/css_font_feature_value.h
// Ported from: blink/renderer/core/css/css_font_variation_value.h
// Ported from: blink/renderer/core/css/css_font_style_range_value.h
// Ported from: blink/renderer/core/css/css_alternate_value.h
// Ported from: blink/renderer/core/css/css_palette_mix_value.h
// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include <cassert>
#include <utility>

#include "base/memory/values_equivalent.h"
#include "base/text/atomic_string.h"
#include "paint/color.h"
#include "style/css_identifier_value.h"
#include "style/css_primitive_value.h"
#include "style/css_value_list.h"

namespace bkit {

class CSSToLengthConversionData;

namespace cssvalue {

// One <feature-tag-value> of font-feature-settings.
class CSSFontFeatureValue : public CSSValue {
public:
  static scoped_refptr<const CSSFontFeatureValue> Create(const AtomicString& tag,
                                                           scoped_refptr<const CSSPrimitiveValue> value) {
    return base::AdoptRef(new CSSFontFeatureValue(tag, std::move(value)));
  }
  CSSFontFeatureValue(const AtomicString& tag, scoped_refptr<const CSSPrimitiveValue> value)
      : CSSValue(kFontFeatureClass), tag_(tag), value_(std::move(value)) {}

  const AtomicString& Tag() const { return tag_; }
  const CSSPrimitiveValue& Value() const { return *value_; }
  int Value(const CSSToLengthConversionData& length_resolver) const { return value_->ComputeInteger(length_resolver); }

  bool Equals(const CSSFontFeatureValue& other) const {
    return tag_ == other.tag_ && base::ValuesEquivalent(value_, other.value_);
  }

private:
  AtomicString tag_;
  scoped_refptr<const CSSPrimitiveValue> value_;
};

// One axis of font-variation-settings.
class CSSFontVariationValue : public CSSValue {
public:
  static scoped_refptr<const CSSFontVariationValue> Create(const AtomicString& tag,
                                                             scoped_refptr<const CSSPrimitiveValue> value) {
    return base::AdoptRef(new CSSFontVariationValue(tag, std::move(value)));
  }
  CSSFontVariationValue(const AtomicString& tag, scoped_refptr<const CSSPrimitiveValue> value)
      : CSSValue(kFontVariationClass), tag_(tag), value_(std::move(value)) {}

  const AtomicString& Tag() const { return tag_; }
  const CSSPrimitiveValue* Value() const { return value_.get(); }

  bool Equals(const CSSFontVariationValue& other) const {
    return tag_ == other.tag_ && base::ValuesEquivalent(value_, other.value_);
  }

private:
  AtomicString tag_;
  scoped_refptr<const CSSPrimitiveValue> value_;
};

// font-style: oblique with an optional angle (a list of at most one).
class CSSFontStyleRangeValue final : public CSSValue {
public:
  static scoped_refptr<const CSSFontStyleRangeValue> Create(
      scoped_refptr<const CSSIdentifierValue> font_style_value,
      scoped_refptr<const CSSValueList> oblique_values = nullptr) {
    return base::AdoptRef(new CSSFontStyleRangeValue(std::move(font_style_value), std::move(oblique_values)));
  }
  CSSFontStyleRangeValue(scoped_refptr<const CSSIdentifierValue> font_style_value,
                         scoped_refptr<const CSSValueList> oblique_values)
      : CSSValue(kFontStyleRangeClass), font_style_value_(std::move(font_style_value)),
        oblique_values_(std::move(oblique_values)) {
    assert(font_style_value_);
  }

  const CSSIdentifierValue* GetFontStyleValue() const { return font_style_value_.get(); }
  const CSSValueList* GetObliqueValues() const { return oblique_values_.get(); }

  bool Equals(const CSSFontStyleRangeValue& other) const {
    return base::ValuesEquivalent(font_style_value_, other.font_style_value_) &&
           base::ValuesEquivalent(oblique_values_, other.oblique_values_);
  }

private:
  scoped_refptr<const CSSIdentifierValue> font_style_value_;
  scoped_refptr<const CSSValueList> oblique_values_;
};

// A functional font-variant-alternates value: the function and its list of
// <custom-ident> aliases.
class CSSAlternateValue : public CSSValue {
public:
  static scoped_refptr<const CSSAlternateValue> Create(scoped_refptr<const CSSFunctionValue> function,
                                                         scoped_refptr<const CSSValueList> alias_list) {
    return base::AdoptRef(new CSSAlternateValue(std::move(function), std::move(alias_list)));
  }
  CSSAlternateValue(scoped_refptr<const CSSFunctionValue> function, scoped_refptr<const CSSValueList> alias_list)
      : CSSValue(kAlternateClass), function_(std::move(function)), aliases_(std::move(alias_list)) {}

  const CSSFunctionValue& Function() const { return *function_; }
  const CSSValueList& Aliases() const { return *aliases_; }

  bool Equals(const CSSAlternateValue& other) const {
    return base::ValuesEquivalent(function_, other.function_) && base::ValuesEquivalent(aliases_, other.aliases_);
  }

private:
  scoped_refptr<const CSSFunctionValue> function_;
  scoped_refptr<const CSSValueList> aliases_;
};

// palette-mix().
class CSSPaletteMixValue : public CSSValue {
public:
  static scoped_refptr<const CSSPaletteMixValue> Create(scoped_refptr<const CSSValue> palette1,
                                                          scoped_refptr<const CSSValue> palette2,
                                                          scoped_refptr<const CSSPrimitiveValue> p1,
                                                          scoped_refptr<const CSSPrimitiveValue> p2,
                                                          Color::ColorSpace color_interpolation_space,
                                                          Color::HueInterpolationMethod hue_interpolation_method) {
    return base::AdoptRef(new CSSPaletteMixValue(std::move(palette1), std::move(palette2), std::move(p1), std::move(p2),
                                                 color_interpolation_space, hue_interpolation_method));
  }
  CSSPaletteMixValue(scoped_refptr<const CSSValue> palette1, scoped_refptr<const CSSValue> palette2,
                     scoped_refptr<const CSSPrimitiveValue> p1, scoped_refptr<const CSSPrimitiveValue> p2,
                     Color::ColorSpace color_interpolation_space, Color::HueInterpolationMethod hue_interpolation_method)
      : CSSValue(kPaletteMixClass), palette1_(std::move(palette1)), palette2_(std::move(palette2)),
        percentage1_(std::move(p1)), percentage2_(std::move(p2)),
        color_interpolation_space_(color_interpolation_space), hue_interpolation_method_(hue_interpolation_method) {}

  const CSSValue& Palette1() const { return *palette1_; }
  const CSSValue& Palette2() const { return *palette2_; }
  const CSSPrimitiveValue* Percentage1() const { return percentage1_.get(); }
  const CSSPrimitiveValue* Percentage2() const { return percentage2_.get(); }
  Color::ColorSpace ColorInterpolationSpace() const { return color_interpolation_space_; }
  Color::HueInterpolationMethod HueInterpolationMethod() const { return hue_interpolation_method_; }

  bool Equals(const CSSPaletteMixValue& other) const {
    return base::ValuesEquivalent(palette1_, other.palette1_) && base::ValuesEquivalent(palette2_, other.palette2_) &&
           base::ValuesEquivalent(percentage1_, other.percentage1_) &&
           base::ValuesEquivalent(percentage2_, other.percentage2_) &&
           color_interpolation_space_ == other.color_interpolation_space_ &&
           hue_interpolation_method_ == other.hue_interpolation_method_;
  }

private:
  scoped_refptr<const CSSValue> palette1_;
  scoped_refptr<const CSSValue> palette2_;
  scoped_refptr<const CSSPrimitiveValue> percentage1_;
  scoped_refptr<const CSSPrimitiveValue> percentage2_;
  Color::ColorSpace color_interpolation_space_;
  Color::HueInterpolationMethod hue_interpolation_method_;
};

} // namespace cssvalue

template <>
struct DowncastTraits<cssvalue::CSSFontFeatureValue> {
  static bool AllowFrom(const CSSValue& value) { return value.IsFontFeatureValue(); }
};
template <>
struct DowncastTraits<cssvalue::CSSFontVariationValue> {
  static bool AllowFrom(const CSSValue& value) { return value.IsFontVariationValue(); }
};
template <>
struct DowncastTraits<cssvalue::CSSFontStyleRangeValue> {
  static bool AllowFrom(const CSSValue& value) { return value.IsFontStyleRangeValue(); }
};
template <>
struct DowncastTraits<cssvalue::CSSAlternateValue> {
  static bool AllowFrom(const CSSValue& value) { return value.IsAlternateValue(); }
};
template <>
struct DowncastTraits<cssvalue::CSSPaletteMixValue> {
  static bool AllowFrom(const CSSValue& value) { return value.IsPaletteMixValue(); }
};

} // namespace bkit
