// Adapted from: blink/renderer/core/css/resolver/style_builder_converter.cc
/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "style_builder_converter.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <map>

#include "base/containers/adapters.h"
#include "base/hash_map.h"
#include "base/math_extras.h"
#include "base/notreached.h"
#include "font/font_family_names.h"
#include "font/font_selector.h"
#include "font/simple_font_data.h"
#include "geometry/length_functions.h"
#include "style/css_color.h"
#include "style/css_font_values.h"
#include "style/css_math_function_value.h"
#include "style/css_numeric_literal_value.h"
#include "style/css_shadow_value.h"
#include "style/css_string_value.h"
#include "style/css_value_id_mappings.h"
#include "style/css_value_pair.h"
#include "style/font_builder.h"
#include "style/style_resolver_state.h"

namespace bkit {
namespace {

const double kFinalStatePercentage2 = 100.0;
const double kMiddleStatePercentage2 = 50.0;

Vector<AtomicString> ValueListToAtomicStringVector(const CSSValueList& value_list) {
  Vector<AtomicString> ret;
  for (const auto& list_entry : value_list) {
    const CSSCustomIdentValue& ident = To<CSSCustomIdentValue>(*list_entry);
    ret.push_back(ident.Value());
  }
  return ret;
}

AtomicString FirstEntryAsAtomicString(const CSSValueList& value_list) {
  assert(value_list.length() == 1u);
  return To<CSSCustomIdentValue>(value_list.Item(0)).Value();
}

FontDescription::GenericFamilyType ConvertGenericFamily(CSSValueID value_id) {
  switch (value_id) {
    case CSSValueID::kWebkitBody: return FontDescription::kWebkitBodyFamily;
    case CSSValueID::kSerif: return FontDescription::kSerifFamily;
    case CSSValueID::kSansSerif: return FontDescription::kSansSerifFamily;
    case CSSValueID::kCursive: return FontDescription::kCursiveFamily;
    case CSSValueID::kFantasy: return FontDescription::kFantasyFamily;
    case CSSValueID::kMonospace: return FontDescription::kMonospaceFamily;
    default: return FontDescription::kNoFamily;
  }
}

bool ConvertFontFamilyName(const CSSValue& value, FontDescription::GenericFamilyType& generic_family,
                           AtomicString& family_name, FontBuilder* font_builder) {
  if (auto* font_family_value = DynamicTo<CSSFontFamilyValue>(value)) {
    generic_family = FontDescription::kNoFamily;
    family_name = font_family_value->Value();
  } else if (font_builder) {
    // TODO(crbug.com/1065468): Get rid of GenericFamilyType.
    auto cssValueID = To<CSSIdentifierValue>(value).GetValueID();
    generic_family = ConvertGenericFamily(cssValueID);
    if (generic_family != FontDescription::kNoFamily) {
      family_name = font_builder->GenericFontFamilyName(generic_family);
    } else if (cssValueID == CSSValueID::kSystemUi) {
      family_name = font_family_names::kSystemUi;
    } else if (cssValueID == CSSValueID::kMath) {
      family_name = font_family_names::kMath;
    }
    // Something went wrong with the conversion or retrieving the name from
    // preferences for the specific generic family.
    if (family_name.empty()) return false;
  }

  // Empty font family names (converted from CSSFontFamilyValue above) are
  // acceptable for defining and matching against
  // @font-faces, compare https://github.com/w3c/csswg-drafts/issues/4510.
  return !family_name.IsNull();
}

std::shared_ptr<const FontPalette> ConvertFontPaletteValue(const CSSToLengthConversionData& length_resolver,
                                                           const CSSValue& value);

// CSSColorMixValue::NormalizePercentages().
bool NormalizePercentages(const CSSPrimitiveValue* percentage1, const CSSPrimitiveValue* percentage2,
                          double& mix_amount, double& alpha_multiplier,
                          const CSSToLengthConversionData& length_resolver) {
  double p1 = 0.5;
  if (percentage1) p1 = ClampTo<double>(percentage1->ComputePercentage(length_resolver), 0.0, 100.0) / 100.0;
  double p2 = 0.5;
  if (percentage2) p2 = ClampTo<double>(percentage2->ComputePercentage(length_resolver), 0.0, 100.0) / 100.0;

  if (percentage1 && !percentage2) p2 = 1.0 - p1;
  else if (percentage2 && !percentage1) p1 = 1.0 - p2;

  if (p1 == 0.0 && p2 == 0.0) return false;

  alpha_multiplier = 1.0;

  double scale = p1 + p2;
  if (scale != 0.0) {
    p1 /= scale;
    p2 /= scale;
    if (scale <= 1.0) alpha_multiplier = scale;
  }

  mix_amount = p2;
  if (p1 == 0.0) mix_amount = 1.0;

  return true;
}

// StyleBuilderConverterBase::ConvertPaletteMix().
std::shared_ptr<const FontPalette> ConvertPaletteMix(const CSSToLengthConversionData& length_resolver,
                                                     const CSSValue& value) {
  auto* palette_mix_value = DynamicTo<cssvalue::CSSPaletteMixValue>(value);
  if (palette_mix_value) {
    std::shared_ptr<const FontPalette> palette1 =
        ConvertFontPaletteValue(length_resolver, palette_mix_value->Palette1());
    // Use normal palette.
    if (palette1 == nullptr) palette1 = FontPalette::Create();
    std::shared_ptr<const FontPalette> palette2 =
        ConvertFontPaletteValue(length_resolver, palette_mix_value->Palette2());
    if (palette2 == nullptr) palette2 = FontPalette::Create();

    Color::ColorSpace color_space = palette_mix_value->ColorInterpolationSpace();
    Color::HueInterpolationMethod hue_interpolation_method = palette_mix_value->HueInterpolationMethod();

    double alpha_multiplier;
    double normalized_percentage;
    if (NormalizePercentages(palette_mix_value->Percentage1(), palette_mix_value->Percentage2(),
                             normalized_percentage, alpha_multiplier, length_resolver)) {
      double percentage1 = kMiddleStatePercentage2;
      double percentage2 = kMiddleStatePercentage2;
      if (palette_mix_value->Percentage1() && palette_mix_value->Percentage2()) {
        percentage1 = palette_mix_value->Percentage1()->ComputePercentage(length_resolver);
        percentage2 = palette_mix_value->Percentage2()->ComputePercentage(length_resolver);
      } else if (palette_mix_value->Percentage1()) {
        percentage1 = palette_mix_value->Percentage1()->ComputePercentage(length_resolver);
        percentage2 = kFinalStatePercentage2 - percentage1;
      } else if (palette_mix_value->Percentage2()) {
        percentage2 = palette_mix_value->Percentage2()->ComputePercentage(length_resolver);
        percentage1 = kFinalStatePercentage2 - percentage2;
      }
      return FontPalette::Mix(palette1, palette2, percentage1, percentage2, normalized_percentage, alpha_multiplier,
                              color_space, hue_interpolation_method);
    }
  }
  return nullptr;
}

// StyleBuilderConverterBase::ConvertFontPalette().
std::shared_ptr<const FontPalette> ConvertFontPaletteValue(const CSSToLengthConversionData& length_resolver,
                                                           const CSSValue& value) {
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  if (identifier_value && identifier_value->GetValueID() == CSSValueID::kNormal) return nullptr;
  if (identifier_value && identifier_value->GetValueID() == CSSValueID::kDark)
    return FontPalette::Create(FontPalette::kDarkPalette);
  if (identifier_value && identifier_value->GetValueID() == CSSValueID::kLight)
    return FontPalette::Create(FontPalette::kLightPalette);
  auto* custom_identifier = DynamicTo<CSSCustomIdentValue>(value);
  if (custom_identifier) return FontPalette::Create(custom_identifier->Value());
  return ConvertPaletteMix(length_resolver, value);
}

float ComputeFontSize(const CSSToLengthConversionData& conversion_data, const CSSPrimitiveValue& primitive_value,
                      const FontDescription::Size& parent_size) {
  if (primitive_value.IsLength() && !primitive_value.IsCalculatedPercentageWithLength())
    return primitive_value.ComputeLength(conversion_data);
  if (primitive_value.IsCalculated()) {
    // ToCalcValue(conversion_data)->Evaluate(parent_size.value).
    return FloatValueForLength(To<CSSMathFunctionValue>(primitive_value).ConvertToLength(conversion_data),
                               parent_size.value);
  }
  NOTREACHED();
}

} // namespace

FontDescription::FamilyDescription StyleBuilderConverter::ConvertFontFamily(StyleResolverState& state,
                                                                            const CSSValue& value) {
  // StyleBuilderConverterBase::ConvertFontFamily(): there is always a
  // Settings object, so generic families resolve through the font builder.
  FontBuilder* font_builder = &state.GetFontBuilder();
  FontDescription::FamilyDescription desc(FontDescription::kNoFamily);

  AtomicString family_name;
  FontFamily::Type family_type = FontFamily::Type::kFamilyName;
  std::shared_ptr<SharedFontFamily> next;
  bool has_value = false;

  const auto& list = To<CSSValueList>(value);
  for (wtf_size_t i = list.length(); i-- > 0;) {
    const CSSValue& family = list.Item(i);
    AtomicString next_family_name;
    FontDescription::GenericFamilyType generic_family = FontDescription::kNoFamily;

    if (!ConvertFontFamilyName(family, generic_family, next_family_name, font_builder)) continue;

    // TODO(crbug.com/1065468): Get rid of GenericFamilyType.
    const bool is_generic = generic_family != FontDescription::kNoFamily || IsA<CSSIdentifierValue>(family);

    // Take the previous value and wrap it in a `SharedFontFamily` adding to
    // the linked list.
    if (has_value) next = SharedFontFamily::Create(family_name, family_type, std::move(next));
    family_name = next_family_name;
    family_type = is_generic ? FontFamily::Type::kGenericFamily : FontFamily::Type::kFamilyName;
    has_value = true;

    if (desc.generic_family == FontDescription::GenericFamilyType::kNoFamily) desc.generic_family = generic_family;
  }

  desc.family = FontFamily(family_name, family_type, std::move(next));
  return desc;
}

FontDescription::Kerning StyleBuilderConverter::ConvertFontKerning(StyleResolverState&, const CSSValue& value) {
  switch (To<CSSIdentifierValue>(value).GetValueID()) {
    case CSSValueID::kAuto: return FontDescription::kAutoKerning;
    case CSSValueID::kNormal: return FontDescription::kNormalKerning;
    case CSSValueID::kNone: return FontDescription::kNoneKerning;
    default: NOTREACHED();
  }
}

FontDescription::FontVariantPosition StyleBuilderConverter::ConvertFontVariantPosition(StyleResolverState&,
                                                                                       const CSSValue& value) {
  switch (To<CSSIdentifierValue>(value).GetValueID()) {
    case CSSValueID::kNormal: return FontDescription::kNormalVariantPosition;
    case CSSValueID::kSub: return FontDescription::kSubVariantPosition;
    case CSSValueID::kSuper: return FontDescription::kSuperVariantPosition;
    default: NOTREACHED();
  }
}

FontVariantEmoji StyleBuilderConverter::ConvertFontVariantEmoji(StyleResolverState&, const CSSValue& value) {
  return To<CSSIdentifierValue>(value).ConvertTo<FontVariantEmoji>();
}

OpticalSizing StyleBuilderConverter::ConvertFontOpticalSizing(StyleResolverState&, const CSSValue& value) {
  switch (To<CSSIdentifierValue>(value).GetValueID()) {
    case CSSValueID::kAuto: return kAutoOpticalSizing;
    case CSSValueID::kNone: return kNoneOpticalSizing;
    default: NOTREACHED();
  }
}

std::shared_ptr<const FontFeatureSettings> StyleBuilderConverter::ConvertFontFeatureSettings(StyleResolverState& state,
                                                                                             const CSSValue& value) {
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  if (identifier_value && identifier_value->GetValueID() == CSSValueID::kNormal) return FontFeatureSettings::Create();

  const auto& list = To<CSSValueList>(value);
  std::map<uint32_t, int> features;
  for (const auto& css_value : list) {
    const auto& feature = To<cssvalue::CSSFontFeatureValue>(*css_value);
    features[AtomicStringToFourByteTag(feature.Tag())] = feature.Value(state.CssToLengthConversionData());
  }

  std::shared_ptr<FontFeatureSettings> settings = FontFeatureSettings::Create();
  for (const auto& [tag, feature_value] : features) settings->Append(FontFeature(tag, feature_value));
  return settings;
}

std::shared_ptr<const FontVariationSettings> StyleBuilderConverter::ConvertFontVariationSettings(
    StyleResolverState& state, const CSSValue& value) {
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  if (identifier_value && identifier_value->GetValueID() == CSSValueID::kNormal)
    return FontBuilder::InitialVariationSettings();

  const auto& list = To<CSSValueList>(value);
  // Use a temporary map to remove duplicate tags, keeping the last
  // occurrence of each.
  std::map<uint32_t, float> axes;
  for (const auto& item : list) {
    const auto& feature = To<cssvalue::CSSFontVariationValue>(*item);
    axes[AtomicStringToFourByteTag(feature.Tag())] =
        ClampTo<float>(feature.Value()->ComputeNumber(state.CssToLengthConversionData()));
  }
  std::shared_ptr<FontVariationSettings> settings = FontVariationSettings::Create();
  for (const auto& [tag, axis_value] : axes) settings->Append(FontVariationAxis(tag, axis_value));
  return settings;
}

std::shared_ptr<const FontPalette> StyleBuilderConverter::ConvertFontPalette(StyleResolverState& state,
                                                                             const CSSValue& value) {
  return ConvertFontPaletteValue(state.CssToLengthConversionData(), value);
}

FontDescription::Size StyleBuilderConverter::ConvertFontSize(StyleResolverState& state, const CSSValue& value) {
  // The root's parent font is the initial font (the document style).
  auto parent_size = state.ParentFontDescription().GetSize();

  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  if (identifier_value && identifier_value->GetValueID() == CSSValueID::kMath) {
    // MathScriptScaleFactor(): math-depth is not ported, so the depths of the
    // parent and this element are equal and the scale factor is 1.
    return FontDescription::Size(0, parent_size.value, parent_size.is_absolute);
  }

  // StyleBuilderConverterBase::ConvertFontSize().
  if (identifier_value) {
    CSSValueID value_id = identifier_value->GetValueID();
    if (FontSizeFunctions::IsValidValueID(value_id))
      return FontDescription::Size(FontSizeFunctions::KeywordSize(value_id), 0.0f, false);
    if (value_id == CSSValueID::kSmaller) return FontDescription::SmallerSize(parent_size);
    if (value_id == CSSValueID::kLarger) return FontDescription::LargerSize(parent_size);
    NOTREACHED();
  }

  const CSSToLengthConversionData conversion_data = state.FontSizeConversionData();
  const auto& primitive_value = To<CSSPrimitiveValue>(value);
  if (primitive_value.IsPercentage()) {
    return FontDescription::Size(
        /*keyword=*/0, static_cast<float>(primitive_value.ComputePercentage(conversion_data) * parent_size.value / 100.0f),
        parent_size.is_absolute);
  }

  // TODO(crbug.com/979895): This is the result of a refactoring, which might
  // have revealed an existing bug with calculated lengths. Investigate.
  const bool is_absolute = parent_size.is_absolute || primitive_value.IsMathFunctionValue() ||
                           !To<CSSNumericLiteralValue>(primitive_value).IsFontRelativeLength() ||
                           To<CSSNumericLiteralValue>(primitive_value).GetType() == CSSPrimitiveValue::UnitType::kRems;
  return FontDescription::Size(/*keyword=*/0, ComputeFontSize(conversion_data, primitive_value, parent_size),
                               is_absolute);
}

FontSizeAdjust StyleBuilderConverter::ConvertFontSizeAdjust(StyleResolverState& state, const CSSValue& value) {
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  if (identifier_value && identifier_value->GetValueID() == CSSValueID::kNone) return FontBuilder::InitialSizeAdjust();

  if (identifier_value && identifier_value->GetValueID() == CSSValueID::kFromFont)
    return FontSizeAdjust(FontSizeAdjust::kFontSizeAdjustNone, FontSizeAdjust::ValueType::kFromFont);

  if (value.IsPrimitiveValue()) {
    const auto& primitive_value = To<CSSPrimitiveValue>(value);
    assert(primitive_value.IsNumber());
    return FontSizeAdjust(ClampTo<float>(primitive_value.ComputeNumber(state.CssToLengthConversionData())));
  }

  assert(value.IsValuePair());
  const auto& pair = To<CSSValuePair>(value);
  auto metric = To<CSSIdentifierValue>(pair.First()).ConvertTo<FontSizeAdjust::Metric>();

  if (pair.Second().IsPrimitiveValue()) {
    const auto& primitive_value = To<CSSPrimitiveValue>(pair.Second());
    assert(primitive_value.IsNumber());
    return FontSizeAdjust(ClampTo<float>(primitive_value.ComputeNumber(state.CssToLengthConversionData())), metric);
  }

  assert(To<CSSIdentifierValue>(pair.Second()).GetValueID() == CSSValueID::kFromFont);
  return FontSizeAdjust(FontSizeAdjust::kFontSizeAdjustNone, metric, FontSizeAdjust::ValueType::kFromFont);
}

FontSelectionValue StyleBuilderConverter::ConvertFontStretch(StyleResolverState& state, const CSSValue& value) {
  if (const auto* primitive_value = DynamicTo<CSSPrimitiveValue>(value)) {
    if (primitive_value->IsPercentage())
      return ClampTo<FontSelectionValue>(primitive_value->ComputePercentage(state.CssToLengthConversionData()));
  }

  // ConvertFontStretchKeyword().
  if (const auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    switch (identifier_value->GetValueID()) {
      case CSSValueID::kUltraCondensed: return kUltraCondensedWidthValue;
      case CSSValueID::kExtraCondensed: return kExtraCondensedWidthValue;
      case CSSValueID::kCondensed: return kCondensedWidthValue;
      case CSSValueID::kSemiCondensed: return kSemiCondensedWidthValue;
      case CSSValueID::kNormal: return kNormalWidthValue;
      case CSSValueID::kSemiExpanded: return kSemiExpandedWidthValue;
      case CSSValueID::kExpanded: return kExpandedWidthValue;
      case CSSValueID::kExtraExpanded: return kExtraExpandedWidthValue;
      case CSSValueID::kUltraExpanded: return kUltraExpandedWidthValue;
      default: break;
    }
  }
  NOTREACHED();
}

FontSelectionValue StyleBuilderConverter::ConvertFontStyle(StyleResolverState& state, const CSSValue& value) {
  assert(!value.IsPrimitiveValue());

  if (const auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    switch (identifier_value->GetValueID()) {
      case CSSValueID::kItalic:
      case CSSValueID::kOblique: return kItalicSlopeValue;
      case CSSValueID::kNormal: return kNormalSlopeValue;
      default: NOTREACHED();
    }
  } else if (const auto* style_range_value = DynamicTo<cssvalue::CSSFontStyleRangeValue>(value)) {
    const CSSValueList* values = style_range_value->GetObliqueValues();
    assert(!values || values->length() < 2u);
    if (values && values->length()) {
      const double angle_degrees = To<CSSPrimitiveValue>(values->Item(0)).ComputeDegrees(state.CssToLengthConversionData());
      // FontStyleObliqueZeroDegreeAsNormal is stable.
      if (angle_degrees == 0.0) return kNormalSlopeValue;
      return FontSelectionValue(angle_degrees);
    }
    const CSSIdentifierValue* style = style_range_value->GetFontStyleValue();
    if (style->GetValueID() == CSSValueID::kNormal) return kNormalSlopeValue;
    if (style->GetValueID() == CSSValueID::kItalic || style->GetValueID() == CSSValueID::kOblique)
      return kItalicSlopeValue;
  }

  NOTREACHED();
}

FontSelectionValue StyleBuilderConverter::ConvertFontWeight(StyleResolverState& state, const CSSValue& value) {
  if (const auto* primitive_value = DynamicTo<CSSPrimitiveValue>(value)) {
    if (primitive_value->IsNumber()) {
      // clamp to [1, 1000] range as per
      // https://drafts.csswg.org/css-fonts/#font-weight-prop.
      return ClampTo<FontSelectionValue>(
          std::clamp(primitive_value->ComputeNumber(state.CssToLengthConversionData()), 1., 1000.));
    }
  }

  const FontSelectionValue parent_weight = state.ParentFontDescription().Weight();
  if (const auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    switch (identifier_value->GetValueID()) {
      case CSSValueID::kNormal: return kNormalWeightValue;
      case CSSValueID::kBold: return kBoldWeightValue;
      case CSSValueID::kBolder: return FontDescription::BolderWeight(parent_weight);
      case CSSValueID::kLighter: return FontDescription::LighterWeight(parent_weight);
      default: NOTREACHED();
    }
  }
  NOTREACHED();
}

FontDescription::FontVariantCaps StyleBuilderConverter::ConvertFontVariantCaps(StyleResolverState&,
                                                                               const CSSValue& value) {
  switch (To<CSSIdentifierValue>(value).GetValueID()) {
    case CSSValueID::kNormal: return FontDescription::kCapsNormal;
    case CSSValueID::kSmallCaps: return FontDescription::kSmallCaps;
    case CSSValueID::kAllSmallCaps: return FontDescription::kAllSmallCaps;
    case CSSValueID::kPetiteCaps: return FontDescription::kPetiteCaps;
    case CSSValueID::kAllPetiteCaps: return FontDescription::kAllPetiteCaps;
    case CSSValueID::kUnicase: return FontDescription::kUnicase;
    case CSSValueID::kTitlingCaps: return FontDescription::kTitlingCaps;
    default: return FontDescription::kCapsNormal;
  }
}

FontDescription::VariantLigatures StyleBuilderConverter::ConvertFontVariantLigatures(StyleResolverState&,
                                                                                     const CSSValue& value) {
  if (const auto* value_list = DynamicTo<CSSValueList>(value)) {
    FontDescription::VariantLigatures ligatures;
    for (wtf_size_t i = 0; i < value_list->length(); ++i) {
      const CSSValue& item = value_list->Item(i);
      switch (To<CSSIdentifierValue>(item).GetValueID()) {
        case CSSValueID::kNoCommonLigatures: ligatures.common = FontDescription::kDisabledLigaturesState; break;
        case CSSValueID::kCommonLigatures: ligatures.common = FontDescription::kEnabledLigaturesState; break;
        case CSSValueID::kNoDiscretionaryLigatures:
          ligatures.discretionary = FontDescription::kDisabledLigaturesState;
          break;
        case CSSValueID::kDiscretionaryLigatures:
          ligatures.discretionary = FontDescription::kEnabledLigaturesState;
          break;
        case CSSValueID::kNoHistoricalLigatures: ligatures.historical = FontDescription::kDisabledLigaturesState; break;
        case CSSValueID::kHistoricalLigatures: ligatures.historical = FontDescription::kEnabledLigaturesState; break;
        case CSSValueID::kNoContextual: ligatures.contextual = FontDescription::kDisabledLigaturesState; break;
        case CSSValueID::kContextual: ligatures.contextual = FontDescription::kEnabledLigaturesState; break;
        default: NOTREACHED();
      }
    }
    return ligatures;
  }

  if (To<CSSIdentifierValue>(value).GetValueID() == CSSValueID::kNone)
    return FontDescription::VariantLigatures(FontDescription::kDisabledLigaturesState);

  assert(To<CSSIdentifierValue>(value).GetValueID() == CSSValueID::kNormal);
  return FontDescription::VariantLigatures();
}

FontVariantNumeric StyleBuilderConverter::ConvertFontVariantNumeric(StyleResolverState&, const CSSValue& value) {
  if (value.IsIdentifierValue()) return FontVariantNumeric();

  FontVariantNumeric variant_numeric;
  for (const auto& feature : To<CSSValueList>(value)) {
    switch (To<CSSIdentifierValue>(*feature).GetValueID()) {
      case CSSValueID::kLiningNums: variant_numeric.SetNumericFigure(FontVariantNumeric::kLiningNums); break;
      case CSSValueID::kOldstyleNums: variant_numeric.SetNumericFigure(FontVariantNumeric::kOldstyleNums); break;
      case CSSValueID::kProportionalNums:
        variant_numeric.SetNumericSpacing(FontVariantNumeric::kProportionalNums);
        break;
      case CSSValueID::kTabularNums: variant_numeric.SetNumericSpacing(FontVariantNumeric::kTabularNums); break;
      case CSSValueID::kDiagonalFractions:
        variant_numeric.SetNumericFraction(FontVariantNumeric::kDiagonalFractions);
        break;
      case CSSValueID::kStackedFractions:
        variant_numeric.SetNumericFraction(FontVariantNumeric::kStackedFractions);
        break;
      case CSSValueID::kOrdinal: variant_numeric.SetOrdinal(FontVariantNumeric::kOrdinalOn); break;
      case CSSValueID::kSlashedZero: variant_numeric.SetSlashedZero(FontVariantNumeric::kSlashedZeroOn); break;
      default: NOTREACHED();
    }
  }
  return variant_numeric;
}

std::shared_ptr<const FontVariantAlternates> StyleBuilderConverter::ConvertFontVariantAlternates(StyleResolverState&,
                                                                                                 const CSSValue& value) {
  std::shared_ptr<FontVariantAlternates> alternates = FontVariantAlternates::Create();
  // See FontVariantAlternates::ParseSingleValue - we either receive the normal
  // identifier or a list of 1 or more elements if it's non normal.
  if (value.IsIdentifierValue()) return nullptr;

  // If it's not the single normal identifier, it has to be a list.
  for (const auto& alternate : To<CSSValueList>(value)) {
    if (const auto* alternate_value = DynamicTo<cssvalue::CSSAlternateValue>(*alternate)) {
      switch (alternate_value->Function().FunctionType()) {
        case CSSValueID::kStylistic:
          alternates->SetStylistic(FirstEntryAsAtomicString(alternate_value->Aliases()));
          break;
        case CSSValueID::kSwash: alternates->SetSwash(FirstEntryAsAtomicString(alternate_value->Aliases())); break;
        case CSSValueID::kOrnaments:
          alternates->SetOrnaments(FirstEntryAsAtomicString(alternate_value->Aliases()));
          break;
        case CSSValueID::kAnnotation:
          alternates->SetAnnotation(FirstEntryAsAtomicString(alternate_value->Aliases()));
          break;
        case CSSValueID::kStyleset:
          alternates->SetStyleset(ValueListToAtomicStringVector(alternate_value->Aliases()));
          break;
        case CSSValueID::kCharacterVariant:
          alternates->SetCharacterVariant(ValueListToAtomicStringVector(alternate_value->Aliases()));
          break;
        default: NOTREACHED();
      }
    }
    if (const auto* alternate_value_ident = DynamicTo<CSSIdentifierValue>(*alternate)) {
      assert(alternate_value_ident->GetValueID() == CSSValueID::kHistoricalForms);
      (void)alternate_value_ident;
      alternates->SetHistoricalForms();
    }
  }

  if (alternates->IsNormal()) return nullptr;
  return alternates;
}

FontVariantEastAsian StyleBuilderConverter::ConvertFontVariantEastAsian(StyleResolverState&, const CSSValue& value) {
  if (value.IsIdentifierValue()) return FontVariantEastAsian();

  FontVariantEastAsian variant_east_asian;
  for (const auto& feature : To<CSSValueList>(value)) {
    switch (To<CSSIdentifierValue>(*feature).GetValueID()) {
      case CSSValueID::kJis78: variant_east_asian.SetForm(FontVariantEastAsian::kJis78); break;
      case CSSValueID::kJis83: variant_east_asian.SetForm(FontVariantEastAsian::kJis83); break;
      case CSSValueID::kJis90: variant_east_asian.SetForm(FontVariantEastAsian::kJis90); break;
      case CSSValueID::kJis04: variant_east_asian.SetForm(FontVariantEastAsian::kJis04); break;
      case CSSValueID::kSimplified: variant_east_asian.SetForm(FontVariantEastAsian::kSimplified); break;
      case CSSValueID::kTraditional: variant_east_asian.SetForm(FontVariantEastAsian::kTraditional); break;
      case CSSValueID::kFullWidth: variant_east_asian.SetWidth(FontVariantEastAsian::kFullWidth); break;
      case CSSValueID::kProportionalWidth: variant_east_asian.SetWidth(FontVariantEastAsian::kProportionalWidth); break;
      case CSSValueID::kRuby: variant_east_asian.SetRuby(true); break;
      default: NOTREACHED();
    }
  }
  return variant_east_asian;
}

StyleHyphenateLimitChars StyleBuilderConverter::ConvertHyphenateLimitChars(StyleResolverState& state,
                                                                           const CSSValue& value) {
  if (value.IsIdentifierValue()) return StyleHyphenateLimitChars();
  const auto& list = To<CSSValueList>(value);
  assert(list.length() >= 1u && list.length() <= 3u);
  Vector<unsigned, 3> values;
  for (const auto& item : list) {
    if (const auto* primitive = DynamicTo<CSSPrimitiveValue>(*item)) {
      values.push_back(primitive->ComputeInteger(state.CssToLengthConversionData()));
      continue;
    }
    if (item->IsIdentifierValue()) {
      values.push_back(0);
      continue;
    }
    NOTREACHED();
  }
  values.Grow(3);
  return StyleHyphenateLimitChars(values[0], values[1], values[2]);
}

Length StyleBuilderConverter::ConvertLength(const StyleResolverState& state, const CSSValue& value) {
  return To<CSSPrimitiveValue>(value).ConvertToLength(state.CssToLengthConversionData());
}

Length StyleBuilderConverter::ConvertLengthOrAuto(const StyleResolverState& state, const CSSValue& value) {
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  if (identifier_value && identifier_value->GetValueID() == CSSValueID::kAuto) return Length::Auto();
  return ConvertLength(state, value);
}

TabSize StyleBuilderConverter::ConvertLengthOrTabSpaces(StyleResolverState& state, const CSSValue& value) {
  const auto& primitive_value = To<CSSPrimitiveValue>(value);
  if (primitive_value.IsNumber())
    return TabSize(ClampTo<float>(primitive_value.ComputeNumber(state.CssToLengthConversionData())),
                   TabSizeValueType::kSpace);
  return TabSize(primitive_value.ComputeLength(state.CssToLengthConversionData()), TabSizeValueType::kLength);
}

Length StyleBuilderConverter::ConvertLineHeight(StyleResolverState& state, const CSSValue& value) {
  // AdjustedZoomConversionData(): the text zoom factor is 1 and
  // text-size-adjust is not ported, so the multiplier is the effective zoom.
  const CSSToLengthConversionData conversion_data =
      state.CssToLengthConversionData().CopyWithAdjustedZoom(state.StyleBuilder().EffectiveZoom());
  if (const auto* primitive_value = DynamicTo<CSSPrimitiveValue>(value)) {
    if (primitive_value->IsLength() && !primitive_value->IsCalculatedPercentageWithLength())
      return Length::Fixed(primitive_value->ComputeLength(conversion_data));
    if (primitive_value->IsNumber())
      return Length::Percent(ClampTo<float>(primitive_value->ComputeNumber(conversion_data) * 100.0));
    float computed_font_size = state.StyleBuilder().GetFontDescription().ComputedSize();
    if (primitive_value->IsPercentage()) {
      return Length::Fixed(
          (computed_font_size * ClampTo<int>(primitive_value->ComputePercentage(conversion_data))) / 100.0f);
    }
    if (primitive_value->IsCalculated()) {
      Length zoomed_length = To<CSSMathFunctionValue>(primitive_value)->ConvertToLength(conversion_data);
      return Length::Fixed(ValueForLength(zoomed_length, LayoutUnit(computed_font_size)).ToFloat());
    }
  }

  assert(To<CSSIdentifierValue>(value).GetValueID() == CSSValueID::kNormal);
  return Length::Auto(); // ComputedStyleInitialValues::InitialLineHeight()
}

Length StyleBuilderConverter::ConvertSpacing(StyleResolverState& state, const CSSValue& value) {
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  if (identifier_value && identifier_value->GetValueID() == CSSValueID::kNormal) return Length::Fixed();
  return ConvertLength(state, value);
}

ShadowData StyleBuilderConverter::ConvertShadow(const CSSToLengthConversionData& conversion_data,
                                                StyleResolverState* state, const CSSValue& value) {
  const auto& shadow = To<CSSShadowValue>(value);
  const Vector2dF offset(shadow.x->ComputeLength(conversion_data), shadow.y->ComputeLength(conversion_data));
  float blur = shadow.blur ? shadow.blur->ComputeLength(conversion_data) : 0;
  float spread = shadow.spread ? shadow.spread->ComputeLength(conversion_data) : 0;
  ShadowStyle shadow_style = shadow.style && shadow.style->GetValueID() == CSSValueID::kInset ? ShadowStyle::kInset
                                                                                             : ShadowStyle::kNormal;
  StyleColorValue color = StyleColorValue::CurrentColor();
  if (shadow.color && state) color = ConvertStyleColor(*state, *shadow.color);
  return ShadowData(offset, blur, spread, shadow_style, color);
}

std::shared_ptr<const ShadowList> StyleBuilderConverter::ConvertShadowList(StyleResolverState& state,
                                                                           const CSSValue& value) {
  if (value.IsIdentifierValue()) return nullptr;
  const auto& list = To<CSSValueList>(value);
  ShadowDataVector shadows;
  shadows.ReserveInitialCapacity(list.length());
  for (const auto& item : list) shadows.push_back(ConvertShadow(state.CssToLengthConversionData(), &state, *item));
  return std::make_shared<const ShadowList>(std::move(shadows));
}

StyleColorValue StyleBuilderConverter::ConvertStyleColor(StyleResolverState&, const CSSValue& value) {
  // ResolveColorValue() for the local color subset.
  if (value.IsIdentifierValue()) return StyleColorValue::CurrentColor();
  return StyleColorValue(To<cssvalue::CSSColor>(value).Value());
}

TextDecorationThickness StyleBuilderConverter::ConvertTextDecorationThickness(StyleResolverState& state,
                                                                              const CSSValue& value) {
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  if (identifier_value && identifier_value->GetValueID() == CSSValueID::kFromFont)
    return TextDecorationThickness(identifier_value->GetValueID());
  return TextDecorationThickness(ConvertLengthOrAuto(state, value));
}

TextEmphasisPosition StyleBuilderConverter::ConvertTextTextEmphasisPosition(StyleResolverState&, const CSSValue& value) {
  const auto& list = To<CSSValueList>(value);
  CSSValueID first = To<CSSIdentifierValue>(list.Item(0)).GetValueID();
  if (list.length() < 2) {
    if (first == CSSValueID::kOver) return TextEmphasisPosition::kOverRight;
    if (first == CSSValueID::kUnder) return TextEmphasisPosition::kUnderRight;
    return TextEmphasisPosition::kOverRight;
  }
  CSSValueID second = To<CSSIdentifierValue>(list.Item(1)).GetValueID();
  if (first == CSSValueID::kOver && second == CSSValueID::kRight) return TextEmphasisPosition::kOverRight;
  if (first == CSSValueID::kOver && second == CSSValueID::kLeft) return TextEmphasisPosition::kOverLeft;
  if (first == CSSValueID::kUnder && second == CSSValueID::kRight) return TextEmphasisPosition::kUnderRight;
  if (first == CSSValueID::kUnder && second == CSSValueID::kLeft) return TextEmphasisPosition::kUnderLeft;
  return TextEmphasisPosition::kOverRight;
}

float StyleBuilderConverter::ConvertLineWidth(StyleResolverState& state, const CSSValue& value) {
  double result = 0;
  if (auto* identifier_value = DynamicTo<CSSIdentifierValue>(value)) {
    switch (identifier_value->GetValueID()) {
      case CSSValueID::kThin: result = 1; break;
      case CSSValueID::kMedium: result = 3; break;
      case CSSValueID::kThick: result = 5; break;
      default: NOTREACHED();
    }
    result = state.CssToLengthConversionData().ZoomedComputedPixels(result, CSSPrimitiveValue::UnitType::kPixels);
  } else {
    result = To<CSSPrimitiveValue>(value).ComputeLengthDouble(state.CssToLengthConversionData());
  }
  double zoomed_result = state.StyleBuilder().EffectiveZoom() * result;
  if (zoomed_result > 0.0 && zoomed_result < 1.0) return 1.0;
  return ClampTo<float>(RoundForImpreciseConversion<float>(result));
}

float StyleBuilderConverter::ConvertTextStrokeWidth(StyleResolverState& state, const CSSValue& value) {
  auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
  if (identifier_value && identifier_value->GetValueID() != CSSValueID::kInvalid) {
    float multiplier = ConvertLineWidth(state, value);
    return CSSNumericLiteralValue::Create(multiplier / 48, CSSPrimitiveValue::UnitType::kEms)
        ->ComputeLength(state.CssToLengthConversionData());
  }
  return To<CSSPrimitiveValue>(value).ComputeLength(state.CssToLengthConversionData());
}

TextUnderlinePosition StyleBuilderConverter::ConvertTextUnderlinePosition(StyleResolverState&, const CSSValue& value) {
  TextUnderlinePosition flags = TextUnderlinePosition::kAuto;
  auto process = [&flags](const CSSValue& identifier) {
    flags |= To<CSSIdentifierValue>(identifier).ConvertTo<TextUnderlinePosition>();
  };
  if (auto* value_list = DynamicTo<CSSValueList>(value)) {
    for (const auto& entry : *value_list) process(*entry);
  } else {
    process(value);
  }
  return flags;
}

Length StyleBuilderConverter::ConvertTextUnderlineOffset(StyleResolverState& state, const CSSValue& value) {
  return ConvertLengthOrAuto(state, value);
}

AtomicString StyleBuilderConverter::ConvertStringOrAuto(StyleResolverState&, const CSSValue& value) {
  if (value.IsIdentifierValue()) return AtomicString();
  return AtomicString(To<CSSStringValue>(value).Value());
}

} // namespace bkit
