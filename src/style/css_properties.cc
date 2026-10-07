// Adapted from the value-building half of the ported properties'
// ParseSingleValue() and ParseShorthand()
// (core/css/properties/longhands/longhands_custom.cc,
// core/css/properties/shorthands/shorthands_custom.cc), the parsers in
// core/css/parser/font_variant_*_parser.*, the css_parsing_utils.cc helpers
// they call and CSSParserFastPaths::IsValidKeywordPropertyAndValue().
#include "css_properties.h"

#include <cmath>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <utility>

#include "base/memory/values_equivalent.h"
#include "base/text/string_view.h"
#include "style/css_color.h"
#include "style/css_font_values.h"
#include "style/css_identifier_value.h"
#include "style/css_initial_value.h"
#include "style/css_numeric_literal_value.h"
#include "style/css_shadow_value.h"
#include "style/css_string_value.h"
#include "style/css_value_id_mappings.h"
#include "style/css_value_list.h"
#include "style/css_value_pair.h"
#include "style/white_space.h"

namespace bkfont {
namespace {

using UnitType = CSSPrimitiveValue::UnitType;
using ValueRange = CSSPrimitiveValue::ValueRange;
using Category = CSSMathFunctionValue::Category;
using ValueRef = scoped_refptr<const CSSValue>;
using PrimitiveRef = scoped_refptr<const CSSPrimitiveValue>;
using V = CSSValueID;

// css_parsing_utils.cc kMinObliqueValue / kMaxObliqueValue.
constexpr double kMinObliqueValue = -90.0;
constexpr double kMaxObliqueValue = 90.0;

// ConsumeIdent<...>().
ValueRef IdentValue(CSSValueID id, std::initializer_list<CSSValueID> allowed) {
  for (CSSValueID keyword : allowed)
    if (id == keyword) return CSSIdentifierValue::Create(id);
  return nullptr;
}

// ConsumeIdentRange().
ValueRef IdentRangeValue(CSSValueID id, CSSValueID lower, CSSValueID upper) {
  if (id < lower || id > upper) return nullptr;
  return CSSIdentifierValue::Create(id);
}

bool IsOneOf(CSSValueID id, std::initializer_list<CSSValueID> allowed) {
  for (CSSValueID keyword : allowed)
    if (id == keyword) return true;
  return false;
}

// A numeric token's value check: kNonNegative rejects a negative value.
// Non-finite values have no token and are rejected.
bool IsInRange(double value, ValueRange range) {
  return std::isfinite(value) && (range != ValueRange::kNonNegative || value >= 0);
}

// The number-token branch of ConsumeNumber().
PrimitiveRef NumberValue(CSSNumber number, ValueRange range) {
  if (!IsInRange(number.value, range)) return nullptr;
  return CSSNumericLiteralValue::Create(number.value, UnitType::kNumber);
}

// The number-token branch of ConsumeIntegerInternal().
PrimitiveRef IntegerValue(CSSInteger integer, double minimum_value) {
  if (integer.value < minimum_value) return nullptr;
  return CSSNumericLiteralValue::Create(integer.value, UnitType::kInteger);
}

// The dimension-token branch of ConsumeLength().
PrimitiveRef LengthValue(CSSLength length, ValueRange range) {
  if (!CSSPrimitiveValue::IsLength(length.unit) || !IsInRange(length.value, range)) return nullptr;
  return CSSNumericLiteralValue::Create(length.value, length.unit);
}

// The percentage-token branch of ConsumePercent().
PrimitiveRef PercentValue(CSSLength percent, ValueRange range) {
  if (percent.unit != UnitType::kPercentage || !IsInRange(percent.value, range)) return nullptr;
  return CSSNumericLiteralValue::Create(percent.value, UnitType::kPercentage);
}

// The token branches of ConsumeLengthOrPercent().
PrimitiveRef LengthOrPercentValue(CSSLength value, ValueRange range) {
  if (value.unit == UnitType::kPercentage) return PercentValue(value, range);
  return LengthValue(value, range);
}

// MathFunctionParser: a math function of one of `categories`, which records
// the `range` of its position.
PrimitiveRef CalcValue(const CSSCalc& calc, std::initializer_list<Category> categories, ValueRange range) {
  for (const CSSMathFunctionValue::Term& term : calc.terms)
    if (!std::isfinite(term.value)) return nullptr;
  const std::optional<Category> category = CSSMathFunctionValue::CategoryOf(calc.terms);
  if (!category) return nullptr;
  for (Category allowed : categories)
    if (*category == allowed) return CSSMathFunctionValue::Create(calc.terms, range);
  return nullptr;
}

// ConsumeLengthOrPercent()'s math function: CanConsumeCalcValue().
PrimitiveRef LengthOrPercentCalcValue(const CSSCalc& calc, ValueRange range) {
  return CalcValue(calc, {Category::kLength, Category::kPercent, Category::kLengthPercent}, range);
}

// ConsumeLength().
PrimitiveRef LengthOrCalcValue(const CSSLengthOrCalc& value, ValueRange range) {
  if (const auto* length = std::get_if<CSSLength>(&value)) return LengthValue(*length, range);
  return CalcValue(std::get<CSSCalc>(value), {Category::kLength}, range);
}

// ConsumeLengthOrPercent() for the variants holding a keyword, a CSSLength
// or a CSSCalc; the keyword is the caller's.
template <typename Input>
PrimitiveRef LengthOrPercentInputValue(const Input& input, ValueRange range) {
  if (const auto* length = std::get_if<CSSLength>(&input)) return LengthOrPercentValue(*length, range);
  return LengthOrPercentCalcValue(std::get<CSSCalc>(input), range);
}

// ConsumeLength() for the same variants.
template <typename Input>
PrimitiveRef LengthInputValue(const Input& input, ValueRange range) {
  if (const auto* length = std::get_if<CSSLength>(&input)) return LengthValue(*length, range);
  return CalcValue(std::get<CSSCalc>(input), {Category::kLength}, range);
}

// ConsumeColor() for the local subset: currentcolor or an RGBA color whose
// components are finite.
ValueRef ColorValue(const StyleColorValue& color) {
  if (!color.IsCurrentColor()) {
    const bkfont::Color rgba = color.GetColor();
    if (!std::isfinite(rgba.Param0()) || !std::isfinite(rgba.Param1()) || !std::isfinite(rgba.Param2()) ||
        !std::isfinite(rgba.Alpha()))
      return nullptr;
  }
  return color.ToCSSValue();
}

// ConsumeString().
ValueRef StringValue(const String& string) {
  if (string.IsNull()) return nullptr;
  return CSSStringValue::Create(string);
}

// ConsumeCustomIdent(): an identifier other than a CSS-wide keyword or
// 'default'. A null or empty name has no identifier token.
bool IsCustomIdent(const AtomicString& name) {
  if (name.empty()) return false;
  for (const char* reserved : {"initial", "inherit", "unset", "revert", "revert-layer", "default"})
    if (EqualIgnoringASCIICase(StringView(name), StringView(reserved))) return false;
  return true;
}

// ConsumeDashedIdent(): a <custom-ident> starting with two dashes.
bool IsDashedIdent(const AtomicString& name) {
  return name.length() > 2 && name[0] == '-' && name[1] == '-' && IsCustomIdent(name);
}

// ConsumeFontSettingsTagAndValue() / ConsumeFontVariationTag(): four
// characters in 0x20-0x7E.
bool IsFontTag(const AtomicString& tag) {
  if (tag.length() != 4) return false;
  for (unsigned i = 0; i < 4; ++i)
    if (tag[i] < 0x20 || tag[i] > 0x7E) return false;
  return true;
}

// The keywords of the FontVariant*Parser classes: distinct groups, in the
// order given. Null for an empty list, a keyword of no group or a repeated
// group.
ValueRef KeywordGroupList(const Vector<CSSValueID>& keywords,
                          std::initializer_list<std::initializer_list<CSSValueID>> groups) {
  if (keywords.empty()) return nullptr;
  Vector<bool> seen(static_cast<wtf_size_t>(groups.size()), false);
  CSSValueList::Values items;
  for (CSSValueID keyword : keywords) {
    wtf_size_t index = 0;
    bool found = false;
    for (const auto& group : groups) {
      if (IsOneOf(keyword, group)) {
        found = true;
        break;
      }
      ++index;
    }
    if (!found || seen[index]) return nullptr;
    seen[index] = true;
    items.push_back(CSSIdentifierValue::Create(keyword));
  }
  return CSSValueList::CreateSpaceSeparated(std::move(items));
}

// FontVariantLigaturesParser.
ValueRef LigaturesList(const Vector<CSSValueID>& keywords) {
  return KeywordGroupList(keywords, {{V::kCommonLigatures, V::kNoCommonLigatures},
                                     {V::kDiscretionaryLigatures, V::kNoDiscretionaryLigatures},
                                     {V::kHistoricalLigatures, V::kNoHistoricalLigatures},
                                     {V::kContextual, V::kNoContextual}});
}

// FontVariantNumericParser.
ValueRef NumericList(const Vector<CSSValueID>& keywords) {
  return KeywordGroupList(keywords, {{V::kLiningNums, V::kOldstyleNums},
                                     {V::kProportionalNums, V::kTabularNums},
                                     {V::kDiagonalFractions, V::kStackedFractions},
                                     {V::kOrdinal},
                                     {V::kSlashedZero}});
}

// FontVariantEastAsianParser: FinalizeValue() stores form, width, ruby.
ValueRef EastAsianList(const Vector<CSSValueID>& keywords) {
  constexpr size_t kGroups = 3;
  const std::initializer_list<CSSValueID> groups[kGroups] = {
      {V::kJis78, V::kJis83, V::kJis90, V::kJis04, V::kSimplified, V::kTraditional},
      {V::kFullWidth, V::kProportionalWidth},
      {V::kRuby}};
  if (keywords.empty()) return nullptr;
  CSSValueID values[kGroups] = {};
  for (CSSValueID keyword : keywords) {
    size_t index = 0;
    while (index < kGroups && !IsOneOf(keyword, groups[index])) ++index;
    if (index == kGroups || values[index] != CSSValueID::kInvalid) return nullptr;
    values[index] = keyword;
  }
  CSSValueList::Values items;
  for (CSSValueID value : values)
    if (value != CSSValueID::kInvalid) items.push_back(CSSIdentifierValue::Create(value));
  return CSSValueList::CreateSpaceSeparated(std::move(items));
}

// FontVariantAlternatesParser: FinalizeValue() stores stylistic,
// historical-forms, styleset, character-variant, swash, ornaments,
// annotation.
ValueRef AlternatesList(const css_longhand::FontVariantAlternates::Alternates& alternates) {
  CSSValueList::Values items;
  bool valid = true;
  const auto add_function = [&](CSSValueID function, std::initializer_list<AtomicString> names) {
    CSSValueList::Values aliases;
    for (const AtomicString& name : names) {
      if (!IsCustomIdent(name)) valid = false;
      aliases.push_back(CSSCustomIdentValue::Create(name));
    }
    items.push_back(cssvalue::CSSAlternateValue::Create(CSSFunctionValue::Create(function, {}),
                                                        CSSValueList::CreateCommaSeparated(std::move(aliases))));
  };
  const auto add_list = [&](CSSValueID function, const Vector<AtomicString>& names) {
    CSSValueList::Values aliases;
    for (const AtomicString& name : names) {
      if (!IsCustomIdent(name)) valid = false;
      aliases.push_back(CSSCustomIdentValue::Create(name));
    }
    items.push_back(cssvalue::CSSAlternateValue::Create(CSSFunctionValue::Create(function, {}),
                                                        CSSValueList::CreateCommaSeparated(std::move(aliases))));
  };
  if (alternates.stylistic) add_function(V::kStylistic, {*alternates.stylistic});
  if (alternates.historical_forms) items.push_back(CSSIdentifierValue::Create(V::kHistoricalForms));
  if (!alternates.styleset.empty()) add_list(V::kStyleset, alternates.styleset);
  if (!alternates.character_variant.empty()) add_list(V::kCharacterVariant, alternates.character_variant);
  if (alternates.swash) add_function(V::kSwash, {*alternates.swash});
  if (alternates.ornaments) add_function(V::kOrnaments, {*alternates.ornaments});
  if (alternates.annotation) add_function(V::kAnnotation, {*alternates.annotation});
  if (!valid || items.empty()) return nullptr;
  return CSSValueList::CreateSpaceSeparated(std::move(items));
}

// ConsumeAngle(stream, context, std::nullopt, kMinObliqueValue,
// kMaxObliqueValue) for font-style.
PrimitiveRef ObliqueAngleValue(const CSSFontStyleOblique& oblique) {
  if (const auto* literal = std::get_if<CSSLength>(&oblique.angle)) {
    // ConsumeNumericLiteralAngle(), without a unitless zero.
    if (!CSSPrimitiveValue::IsAngle(literal->unit) || !std::isfinite(literal->value)) return nullptr;
    return CSSNumericLiteralValue::Create(literal->value, literal->unit);
  }
  // ConsumeMathFunctionAngle(): the math function keeps kAll, but one that
  // simplifies to a value outside the limits becomes that limit in degrees.
  // A local sum of angles always simplifies to a literal in degrees.
  const CSSCalc& calc = std::get<CSSCalc>(oblique.angle);
  PrimitiveRef angle = CalcValue(calc, {Category::kAngle}, ValueRange::kAll);
  if (!angle) return nullptr;
  double degrees = 0;
  for (const CSSMathFunctionValue::Term& term : calc.terms)
    degrees += term.value * CSSPrimitiveValue::ConversionToCanonicalUnitsScaleFactor(term.unit);
  if (degrees < kMinObliqueValue) return CSSNumericLiteralValue::Create(kMinObliqueValue, UnitType::kDegrees);
  if (degrees > kMaxObliqueValue) return CSSNumericLiteralValue::Create(kMaxObliqueValue, UnitType::kDegrees);
  return angle;
}

// IsAngleWithinLimits(): a literal's number in its own unit, so 2rad passes
// and 100grad fails. A math function passes.
bool IsAngleWithinLimits(const CSSValue& angle) {
  constexpr float kMaxAngle = 90.0f;
  const auto* numeric_angle = DynamicTo<CSSNumericLiteralValue>(angle);
  if (!numeric_angle) return true;
  return numeric_angle->DoubleValue() >= -kMaxAngle && numeric_angle->DoubleValue() <= kMaxAngle;
}

// IsAngleZero() (FontStyleObliqueZeroDegreeAsNormal).
bool IsAngleZero(const CSSValue& angle) {
  const auto* numeric_angle = DynamicTo<CSSNumericLiteralValue>(angle);
  return numeric_angle && numeric_angle->DoubleValue() == 0.0;
}

// CssValueIDToPlatformEnum<EWhiteSpace>() for the predefined keywords.
std::optional<EWhiteSpace> PredefinedWhiteSpace(CSSValueID id) {
  switch (id) {
    case CSSValueID::kNormal: return EWhiteSpace::kNormal;
    case CSSValueID::kNowrap: return EWhiteSpace::kNowrap;
    case CSSValueID::kPre: return EWhiteSpace::kPre;
    case CSSValueID::kPreLine: return EWhiteSpace::kPreLine;
    case CSSValueID::kPreWrap: return EWhiteSpace::kPreWrap;
    case CSSValueID::kBreakSpaces: return EWhiteSpace::kBreakSpaces;
    default: return std::nullopt;
  }
}

// ConsumeShorthandGreedilyViaLonghands(): at least one component; an
// omitted one is the initial value. `components` are in shorthand order.
bool GreedyShorthand(std::initializer_list<std::pair<CSSPropertyID, std::optional<ValueRef>>> components,
                     CSSPropertyValues& properties) {
  bool found_any = false;
  for (const auto& [property, value] : components) {
    if (!value) continue;
    if (!*value) return false;
    found_any = true;
  }
  if (!found_any) return false;
  for (const auto& [property, value] : components)
    properties.push_back(CSSPropertyValue{property, value ? *value : ValueRef(CSSInitialValue::Create())});
  return true;
}

// The value of an optional greedy component: nullopt when omitted, null
// when invalid.
template <typename Input, typename MakeFunction>
std::optional<ValueRef> Component(const std::optional<Input>& input, MakeFunction make) {
  if (!input) return std::nullopt;
  return ValueRef(make(*input));
}

} // namespace

bool CSSPropertyValue::operator==(const CSSPropertyValue& other) const {
  return property == other.property && base::ValuesEquivalent(value, other.value);
}

namespace css_longhand {

template <CSSPropertyID property>
scoped_refptr<const CSSValue> ColorLonghand<property>::Make(const Input& color) {
  return ColorValue(color);
}

template struct ColorLonghand<CSSPropertyID::kColor>;
template struct ColorLonghand<CSSPropertyID::kTextDecorationColor>;
template struct ColorLonghand<CSSPropertyID::kTextEmphasisColor>;
template struct ColorLonghand<CSSPropertyID::kWebkitTextFillColor>;
template struct ColorLonghand<CSSPropertyID::kWebkitTextStrokeColor>;

ValueRef Direction::Make(Input id) {
  return IdentValue(id, {V::kLtr, V::kRtl});
}

ValueRef FontFamily::Make(const Input& families) {
  if (families.empty()) return nullptr;
  CSSValueList::Values items;
  for (const CSSFontFamilyName& family : families) {
    if (family.generic != CSSValueID::kInvalid) {
      // ConsumeGenericFamily().
      ValueRef generic = IdentRangeValue(family.generic, V::kSerif, V::kMath);
      if (!generic) return nullptr;
      items.push_back(std::move(generic));
    } else {
      // ConsumeFamilyName(). Which unquoted identifiers may form a name is a
      // matter of the tokens; a null name has no token.
      if (family.name.IsNull()) return nullptr;
      items.push_back(CSSFontFamilyValue::Create(family.name));
    }
  }
  return CSSValueList::CreateCommaSeparated(std::move(items));
}

ValueRef FontFeatureSettings::Make(const Input& input) {
  if (const auto* id = std::get_if<CSSValueID>(&input)) return IdentValue(*id, {V::kNormal});
  const Vector<Feature>& features = std::get<Vector<Feature>>(input);
  if (features.empty()) return nullptr;
  CSSValueList::Values items;
  for (const Feature& feature : features) {
    if (!IsFontTag(feature.tag)) return nullptr;
    PrimitiveRef value;
    if (const auto* integer = std::get_if<CSSInteger>(&feature.value)) {
      // ConsumeInteger(stream, context).
      value = IntegerValue(*integer, -std::numeric_limits<double>::max());
    } else if (const auto* calc = std::get_if<CSSCalc>(&feature.value)) {
      value = CalcValue(*calc, {Category::kNumber}, ValueRange::kInteger);
    } else if (const auto* id = std::get_if<CSSValueID>(&feature.value)) {
      // 'on' and 'off' are the numbers 1 and 0.
      if (*id != V::kOn && *id != V::kOff) return nullptr;
      value = CSSNumericLiteralValue::Create(*id == V::kOn, UnitType::kNumber);
    } else {
      value = CSSNumericLiteralValue::Create(1, UnitType::kNumber);
    }
    if (!value) return nullptr;
    items.push_back(cssvalue::CSSFontFeatureValue::Create(feature.tag, std::move(value)));
  }
  return CSSValueList::CreateCommaSeparated(std::move(items));
}

ValueRef FontKerning::Make(Input id) {
  return IdentValue(id, {V::kAuto, V::kNormal, V::kNone});
}

ValueRef FontOpticalSizing::Make(Input id) {
  return IdentValue(id, {V::kAuto, V::kNone});
}

ValueRef FontPalette::Make(const Input& input) {
  if (const auto* id = std::get_if<CSSValueID>(&input)) return IdentValue(*id, {V::kNormal, V::kLight, V::kDark});
  if (const auto* name = std::get_if<AtomicString>(&input)) {
    if (!IsDashedIdent(*name)) return nullptr;
    return CSSCustomIdentValue::Create(*name);
  }
  const Mix* mix = std::get<std::unique_ptr<Mix>>(input).get();
  if (!mix) return nullptr;
  // ConsumeColorInterpolationSpace(): a hue method needs a polar space.
  using ColorSpace = bkfont::Color::ColorSpace;
  const bool polar = mix->color_space == ColorSpace::kHSL || mix->color_space == ColorSpace::kHWB ||
                     mix->color_space == ColorSpace::kLch || mix->color_space == ColorSpace::kOklch;
  if (!polar && mix->hue_interpolation != bkfont::Color::HueInterpolationMethod::kShorter) return nullptr;
  // ConsumePercent(kAll), then the [0, 100] check of a literal.
  const auto percentage = [](const std::optional<CSSLengthOrCalc>& component, bool& valid) -> PrimitiveRef {
    if (!component) return nullptr;
    PrimitiveRef value;
    if (const auto* literal = std::get_if<CSSLength>(&*component)) {
      value = PercentValue(*literal, ValueRange::kAll);
      if (value && (literal->value < 0.0 || literal->value > 100.0)) value = nullptr;
    } else {
      value = CalcValue(std::get<CSSCalc>(*component), {Category::kPercent}, ValueRange::kAll);
    }
    if (!value) valid = false;
    return value;
  };
  bool valid = true;
  ValueRef palette1 = Make(mix->palette1);
  ValueRef palette2 = Make(mix->palette2);
  PrimitiveRef percentage1 = percentage(mix->percentage1, valid);
  PrimitiveRef percentage2 = percentage(mix->percentage2, valid);
  if (!valid || !palette1 || !palette2) return nullptr;
  // Both literally zero (not calc()) is rejected.
  const auto* literal1 = DynamicTo<CSSNumericLiteralValue>(percentage1.get());
  const auto* literal2 = DynamicTo<CSSNumericLiteralValue>(percentage2.get());
  if (literal1 && literal2 && literal1->DoubleValue() == 0.0 && literal2->DoubleValue() == 0.0) return nullptr;
  return cssvalue::CSSPaletteMixValue::Create(std::move(palette1), std::move(palette2), std::move(percentage1),
                                              std::move(percentage2), mix->color_space, mix->hue_interpolation);
}

ValueRef FontSize::Make(const Input& input) {
  if (const auto* id = std::get_if<CSSValueID>(&input)) {
    if (*id == V::kMath) return CSSIdentifierValue::Create(*id);
    return IdentRangeValue(*id, V::kXxSmall, V::kWebkitXxxLarge);
  }
  return LengthOrPercentInputValue(input, ValueRange::kNonNegative);
}

ValueRef FontSizeAdjust::Make(const Input& input) {
  // ConsumeNumber(kNonNegative), or from-font.
  const auto adjust = [](const auto& value) -> ValueRef {
    if (const auto* id = std::get_if<CSSValueID>(&value)) return IdentValue(*id, {V::kFromFont});
    if (const auto* number = std::get_if<CSSNumber>(&value)) return NumberValue(*number, ValueRange::kNonNegative);
    if (const auto* calc = std::get_if<CSSCalc>(&value))
      return CalcValue(*calc, {Category::kNumber}, ValueRange::kNonNegative);
    return nullptr;
  };
  if (const auto* id = std::get_if<CSSValueID>(&input)) {
    if (*id == V::kNone) return CSSIdentifierValue::Create(*id);
    return adjust(input);
  }
  const auto* with_metric = std::get_if<WithMetric>(&input);
  if (!with_metric) return adjust(input);
  if (!IsOneOf(with_metric->metric, {V::kExHeight, V::kCapHeight, V::kChWidth, V::kIcWidth, V::kIcHeight}))
    return nullptr;
  ValueRef value = adjust(with_metric->value);
  if (!value || with_metric->metric == V::kExHeight) return value;
  return CSSValuePair::Create(CSSIdentifierValue::Create(with_metric->metric), std::move(value),
                              CSSValuePair::kKeepIdenticalValues);
}

ValueRef FontStretch::Make(const Input& input) {
  if (const auto* id = std::get_if<CSSValueID>(&input)) return MakeKeywordOnly(*id);
  if (const auto* percent = std::get_if<CSSLength>(&input)) return PercentValue(*percent, ValueRange::kNonNegative);
  return CalcValue(std::get<CSSCalc>(input), {Category::kPercent}, ValueRange::kNonNegative);
}

ValueRef FontStretch::MakeKeywordOnly(CSSValueID id) {
  if (id == V::kNormal) return CSSIdentifierValue::Create(id);
  return IdentRangeValue(id, V::kUltraCondensed, V::kUltraExpanded);
}

ValueRef FontStyle::Make(const Input& input) {
  if (const auto* id = std::get_if<CSSValueID>(&input)) return IdentValue(*id, {V::kNormal, V::kItalic, V::kOblique});
  PrimitiveRef angle = ObliqueAngleValue(std::get<CSSFontStyleOblique>(input));
  if (!angle || !IsAngleWithinLimits(*angle)) return nullptr;
  if (IsAngleZero(*angle)) return CSSIdentifierValue::Create(V::kNormal);
  CSSValueList::Values angles;
  angles.push_back(std::move(angle));
  return cssvalue::CSSFontStyleRangeValue::Create(CSSIdentifierValue::Create(V::kOblique),
                                                  CSSValueList::CreateSpaceSeparated(std::move(angles)));
}

ValueRef FontSynthesisSmallCaps::Make(Input id) {
  return IdentValue(id, {V::kAuto, V::kNone});
}

ValueRef FontSynthesisStyle::Make(Input id) {
  return IdentValue(id, {V::kAuto, V::kNone});
}

ValueRef FontSynthesisWeight::Make(Input id) {
  return IdentValue(id, {V::kAuto, V::kNone});
}

ValueRef FontVariantAlternates::Make(const Input& input) {
  if (const auto* id = std::get_if<CSSValueID>(&input)) return IdentValue(*id, {V::kNormal});
  return AlternatesList(std::get<Alternates>(input));
}

ValueRef FontVariantCaps::Make(Input id) {
  return IdentValue(id, {V::kNormal, V::kSmallCaps, V::kAllSmallCaps, V::kPetiteCaps, V::kAllPetiteCaps,
                         V::kUnicase, V::kTitlingCaps});
}

ValueRef FontVariantEastAsian::Make(const Input& input) {
  if (const auto* id = std::get_if<CSSValueID>(&input)) return IdentValue(*id, {V::kNormal});
  return EastAsianList(std::get<Vector<CSSValueID>>(input));
}

ValueRef FontVariantEmoji::Make(Input id) {
  return IdentValue(id, {V::kNormal, V::kText, V::kEmoji, V::kUnicode});
}

ValueRef FontVariantLigatures::Make(const Input& input) {
  if (const auto* id = std::get_if<CSSValueID>(&input)) return IdentValue(*id, {V::kNormal, V::kNone});
  return LigaturesList(std::get<Vector<CSSValueID>>(input));
}

ValueRef FontVariantNumeric::Make(const Input& input) {
  if (const auto* id = std::get_if<CSSValueID>(&input)) return IdentValue(*id, {V::kNormal});
  return NumericList(std::get<Vector<CSSValueID>>(input));
}

ValueRef FontVariantPosition::Make(Input id) {
  return IdentValue(id, {V::kNormal, V::kSub, V::kSuper});
}

ValueRef FontVariationSettings::Make(const Input& input) {
  if (const auto* id = std::get_if<CSSValueID>(&input)) return IdentValue(*id, {V::kNormal});
  const Vector<Axis>& axes = std::get<Vector<Axis>>(input);
  if (axes.empty()) return nullptr;
  CSSValueList::Values items;
  for (const Axis& axis : axes) {
    if (!IsFontTag(axis.tag)) return nullptr;
    // ConsumeNumber(kAll).
    PrimitiveRef value;
    if (const auto* number = std::get_if<CSSNumber>(&axis.value))
      value = NumberValue(*number, ValueRange::kAll);
    else
      value = CalcValue(std::get<CSSCalc>(axis.value), {Category::kNumber}, ValueRange::kAll);
    if (!value) return nullptr;
    items.push_back(cssvalue::CSSFontVariationValue::Create(axis.tag, std::move(value)));
  }
  return CSSValueList::CreateCommaSeparated(std::move(items));
}

ValueRef FontWeight::Make(const Input& input) {
  if (const auto* id = std::get_if<CSSValueID>(&input)) return IdentRangeValue(*id, V::kNormal, V::kLighter);
  if (const auto* number = std::get_if<CSSNumber>(&input)) {
    // A number token outside [1, 1000] is not a weight (so that font: 0/0
    // reads the zero as the size).
    if (number->value < 1 || number->value > 1000) return nullptr;
    return NumberValue(*number, ValueRange::kNonNegative);
  }
  return CalcValue(std::get<CSSCalc>(input), {Category::kNumber}, ValueRange::kNonNegative);
}

ValueRef HyphenateCharacter::Make(const Input& input) {
  if (const auto* id = std::get_if<CSSValueID>(&input)) return IdentValue(*id, {V::kAuto});
  return StringValue(std::get<String>(input));
}

ValueRef HyphenateLimitChars::Make(const Input& values) {
  if (values.empty() || values.size() > 3) return nullptr;
  CSSValueList::Values items;
  for (const auto& value : values) {
    // ConsumeIntegerOrNumberCalc(kPositiveInteger), or auto.
    ValueRef item;
    if (const auto* integer = std::get_if<CSSInteger>(&value))
      item = IntegerValue(*integer, 1);
    else if (const auto* calc = std::get_if<CSSCalc>(&value))
      item = CalcValue(*calc, {Category::kNumber}, ValueRange::kPositiveInteger);
    else
      item = IdentValue(std::get<CSSValueID>(value), {V::kAuto});
    if (!item) return nullptr;
    items.push_back(std::move(item));
  }
  return CSSValueList::CreateSpaceSeparated(std::move(items));
}

ValueRef Hyphens::Make(Input id) {
  return IdentValue(id, {V::kAuto, V::kNone, V::kManual});
}

ValueRef LetterSpacing::Make(const Input& input) {
  if (const auto* id = std::get_if<CSSValueID>(&input)) return IdentValue(*id, {V::kNormal});
  return LengthInputValue(input, ValueRange::kAll);
}

ValueRef LineBreak::Make(Input id) {
  return IdentValue(id, {V::kAuto, V::kLoose, V::kNormal, V::kStrict, V::kAnywhere});
}

ValueRef LineHeight::Make(const Input& input) {
  if (const auto* id = std::get_if<CSSValueID>(&input)) return IdentValue(*id, {V::kNormal});
  if (const auto* number = std::get_if<CSSNumber>(&input)) return NumberValue(*number, ValueRange::kNonNegative);
  if (const auto* length = std::get_if<CSSLength>(&input)) return LengthOrPercentValue(*length, ValueRange::kNonNegative);
  // ConsumeNumber()'s math function, then ConsumeLengthOrPercent()'s.
  const CSSCalc& calc = std::get<CSSCalc>(input);
  if (PrimitiveRef number = CalcValue(calc, {Category::kNumber}, ValueRange::kNonNegative)) return number;
  return LengthOrPercentCalcValue(calc, ValueRange::kNonNegative);
}

ValueRef OverflowWrap::Make(Input id) {
  return IdentValue(id, {V::kNormal, V::kBreakWord, V::kAnywhere});
}

ValueRef TabSize::Make(const Input& input) {
  if (const auto* number = std::get_if<CSSNumber>(&input)) return NumberValue(*number, ValueRange::kNonNegative);
  if (const auto* length = std::get_if<CSSLength>(&input)) return LengthValue(*length, ValueRange::kNonNegative);
  // ConsumeNumber()'s math function, then ConsumeLength()'s.
  return CalcValue(std::get<CSSCalc>(input), {Category::kNumber, Category::kLength}, ValueRange::kNonNegative);
}

ValueRef TextAlign::Make(Input id) {
  if (id == V::kStart || id == V::kEnd) return CSSIdentifierValue::Create(id);
  return IdentRangeValue(id, V::kWebkitAuto, V::kInternalCenter);
}

ValueRef TextAlignLast::Make(Input id) {
  if (id == V::kStart || id == V::kEnd || id == V::kAuto) return CSSIdentifierValue::Create(id);
  return IdentRangeValue(id, V::kLeft, V::kJustify);
}

ValueRef TextAutospace::Make(Input id) {
  return IdentValue(id, {V::kNormal, V::kNoAutospace});
}

ValueRef TextCombineUpright::Make(Input id) {
  return IdentValue(id, {V::kNone, V::kAll});
}

ValueRef TextDecorationLine::Make(const Input& input) {
  const CSSValueID* keyword = std::get_if<CSSValueID>(&input);
  const Vector<CSSValueID>* lines = std::get_if<Vector<CSSValueID>>(&input);
  if (lines && lines->size() == 1 && IsOneOf((*lines)[0], {V::kNone, V::kSpellingError, V::kGrammarError}))
    keyword = &(*lines)[0];
  if (keyword) {
    if (*keyword == V::kNone) return CSSIdentifierValue::Create(*keyword);
    // Values other than 'none' are in a list (StyleBuilderConverter::
    // ConvertFlags()).
    if (*keyword != V::kSpellingError && *keyword != V::kGrammarError) return nullptr;
    return CSSValueList::CreateSpaceSeparated({CSSIdentifierValue::Create(*keyword)});
  }
  static constexpr CSSValueID kOrder[] = {V::kUnderline, V::kOverline, V::kLineThrough, V::kBlink};
  bool seen[std::size(kOrder)] = {};
  for (CSSValueID line : *lines) {
    size_t index = 0;
    while (index < std::size(kOrder) && kOrder[index] != line) ++index;
    if (index == std::size(kOrder) || seen[index]) return nullptr;
    seen[index] = true;
  }
  CSSValueList::Values items;
  for (size_t i = 0; i < std::size(kOrder); ++i)
    if (seen[i]) items.push_back(CSSIdentifierValue::Create(kOrder[i]));
  if (items.empty()) return nullptr;
  return CSSValueList::CreateSpaceSeparated(std::move(items));
}

ValueRef TextDecorationSkipInk::Make(Input id) {
  return IdentValue(id, {V::kAuto, V::kNone});
}

ValueRef TextDecorationStyle::Make(Input id) {
  return IdentValue(id, {V::kSolid, V::kDouble, V::kDotted, V::kDashed, V::kWavy});
}

ValueRef TextDecorationThickness::Make(const Input& input) {
  if (const auto* id = std::get_if<CSSValueID>(&input)) return IdentValue(*id, {V::kFromFont, V::kAuto});
  return LengthOrPercentInputValue(input, ValueRange::kAll);
}

ValueRef TextEmphasisPosition::Make(const Input& input) {
  if (!IsOneOf(input.over_under, {V::kOver, V::kUnder})) return nullptr;
  CSSValueList::Values items;
  items.push_back(CSSIdentifierValue::Create(input.over_under));
  if (input.left_right != V::kInvalid) {
    if (!IsOneOf(input.left_right, {V::kLeft, V::kRight})) return nullptr;
    items.push_back(CSSIdentifierValue::Create(input.left_right));
  }
  return CSSValueList::CreateSpaceSeparated(std::move(items));
}

ValueRef TextEmphasisStyle::Make(const Input& input) {
  const auto is_fill = [](CSSValueID id) {
    return IsOneOf(id, {V::kFilled, V::kOpen});
  };
  const auto is_shape = [](CSSValueID id) {
    return IsOneOf(id, {V::kDot, V::kCircle, V::kDoubleCircle, V::kTriangle, V::kSesame});
  };
  if (const auto* id = std::get_if<CSSValueID>(&input)) {
    if (*id == V::kNone || is_fill(*id) || is_shape(*id)) return CSSIdentifierValue::Create(*id);
    return nullptr;
  }
  if (const auto* string = std::get_if<String>(&input)) return StringValue(*string);
  const FillAndShape& both = std::get<FillAndShape>(input);
  if (!is_fill(both.fill) || !is_shape(both.shape)) return nullptr;
  return CSSValueList::CreateSpaceSeparated(
      {CSSIdentifierValue::Create(both.fill), CSSIdentifierValue::Create(both.shape)});
}

ValueRef TextIndent::Make(const Input& input) {
  PrimitiveRef length = LengthOrPercentInputValue(input, ValueRange::kAll);
  if (!length) return nullptr;
  return CSSValueList::CreateSpaceSeparated({std::move(length)});
}

ValueRef TextOrientation::Make(Input id) {
  return IdentValue(id, {V::kMixed, V::kUpright, V::kSideways, V::kSidewaysRight});
}

ValueRef TextRendering::Make(Input id) {
  return IdentValue(id, {V::kAuto, V::kOptimizespeed, V::kOptimizelegibility, V::kGeometricprecision});
}

ValueRef TextShadow::Make(const Input& input) {
  if (const auto* id = std::get_if<CSSValueID>(&input)) return IdentValue(*id, {V::kNone});
  const Vector<Shadow>& shadows = std::get<Vector<Shadow>>(input);
  if (shadows.empty()) return nullptr;
  CSSValueList::Values items;
  for (const Shadow& shadow : shadows) {
    PrimitiveRef x = LengthOrCalcValue(shadow.x, ValueRange::kAll);
    PrimitiveRef y = LengthOrCalcValue(shadow.y, ValueRange::kAll);
    PrimitiveRef blur = shadow.blur ? LengthOrCalcValue(*shadow.blur, ValueRange::kNonNegative) : nullptr;
    ValueRef color = shadow.color ? ColorValue(*shadow.color) : nullptr;
    if (!x || !y || (shadow.blur && !blur) || (shadow.color && !color)) return nullptr;
    items.push_back(CSSShadowValue::Create(std::move(x), std::move(y), std::move(blur), nullptr, nullptr,
                                           std::move(color)));
  }
  return CSSValueList::CreateCommaSeparated(std::move(items));
}

ValueRef TextSpacingTrim::Make(Input id) {
  return IdentValue(id, {V::kNormal, V::kTrimStart, V::kSpaceAll, V::kSpaceFirst});
}

ValueRef TextTransform::Make(Input id) {
  if (id == V::kNone) return CSSIdentifierValue::Create(id);
  return IdentRangeValue(id, V::kCapitalize, V::kMathAuto);
}

ValueRef TextUnderlineOffset::Make(const Input& input) {
  if (const auto* id = std::get_if<CSSValueID>(&input)) return IdentValue(*id, {V::kAuto});
  return LengthOrPercentInputValue(input, ValueRange::kAll);
}

ValueRef TextUnderlinePosition::Make(const Input& input) {
  if (const auto* id = std::get_if<CSSValueID>(&input)) return IdentValue(*id, {V::kAuto});
  const Parts& parts = std::get<Parts>(input);
  if (parts.position == V::kInvalid && parts.side == V::kInvalid) return nullptr;
  CSSValueList::Values items;
  if (parts.position != V::kInvalid) {
    if (!IsOneOf(parts.position, {V::kFromFont, V::kUnder})) return nullptr;
    items.push_back(CSSIdentifierValue::Create(parts.position));
  }
  if (parts.side != V::kInvalid) {
    if (!IsOneOf(parts.side, {V::kLeft, V::kRight})) return nullptr;
    items.push_back(CSSIdentifierValue::Create(parts.side));
  }
  return CSSValueList::CreateSpaceSeparated(std::move(items));
}

ValueRef TextWrapMode::Make(Input id) {
  return IdentValue(id, {V::kWrap, V::kNowrap});
}

ValueRef TextWrapStyle::Make(Input id) {
  return IdentValue(id, {V::kAuto, V::kBalance, V::kPretty, V::kStable});
}

ValueRef UnicodeBidi::Make(Input id) {
  return IdentValue(id, {V::kNormal, V::kEmbed, V::kBidiOverride, V::kWebkitIsolate, V::kWebkitIsolateOverride,
                         V::kWebkitPlaintext, V::kIsolate, V::kIsolateOverride, V::kPlaintext});
}

ValueRef VerticalAlign::Make(const Input& input) {
  if (const auto* id = std::get_if<CSSValueID>(&input)) return IdentRangeValue(*id, V::kBaseline, V::kWebkitBaselineMiddle);
  return LengthOrPercentInputValue(input, ValueRange::kAll);
}

ValueRef Visibility::Make(Input id) {
  return IdentValue(id, {V::kVisible, V::kHidden, V::kCollapse});
}

ValueRef WebkitFontSmoothing::Make(Input id) {
  return IdentValue(id, {V::kAuto, V::kNone, V::kAntialiased, V::kSubpixelAntialiased});
}

ValueRef WebkitLineBreak::Make(Input id) {
  return IdentValue(id, {V::kAuto, V::kLoose, V::kNormal, V::kStrict, V::kAfterWhiteSpace});
}

ValueRef WebkitLocale::Make(const Input& input) {
  if (const auto* id = std::get_if<CSSValueID>(&input)) return IdentValue(*id, {V::kAuto});
  return StringValue(std::get<String>(input));
}

ValueRef WebkitTextOrientation::Make(Input id) {
  return IdentValue(id, {V::kSideways, V::kSidewaysRight, V::kVerticalRight, V::kUpright});
}

ValueRef WebkitTextStrokeWidth::Make(const Input& input) {
  if (const auto* id = std::get_if<CSSValueID>(&input)) return IdentValue(*id, {V::kThin, V::kMedium, V::kThick});
  return LengthInputValue(input, ValueRange::kNonNegative);
}

ValueRef WebkitWritingMode::Make(Input id) {
  return IdentRangeValue(id, V::kHorizontalTb, V::kVerticalLr);
}

ValueRef WhiteSpaceCollapse::Make(Input id) {
  return IdentValue(id, {V::kCollapse, V::kPreserve, V::kPreserveBreaks, V::kBreakSpaces});
}

ValueRef WordBreak::Make(Input id) {
  return IdentValue(id, {V::kNormal, V::kBreakAll, V::kKeepAll, V::kBreakWord, V::kAutoPhrase});
}

ValueRef WordSpacing::Make(const Input& input) {
  if (const auto* id = std::get_if<CSSValueID>(&input)) return IdentValue(*id, {V::kNormal});
  return LengthInputValue(input, ValueRange::kAll);
}

ValueRef WritingMode::Make(Input id) {
  return IdentValue(id, {V::kHorizontalTb, V::kVerticalRl, V::kVerticalLr, V::kSidewaysRl, V::kSidewaysLr, V::kLrTb,
                         V::kRlTb, V::kTbRl, V::kLr, V::kRl, V::kTb});
}

} // namespace css_longhand

namespace css_shorthand {

bool Font::Make(const Input& input, CSSPropertyValues& properties) {
  const auto keyword = [](CSSValueID id) -> ValueRef {
    return CSSIdentifierValue::Create(id);
  };
  // The optional components. ConsumeFont() reads a leading 'normal' as no
  // component at all, which gives the same 'normal' as the longhand.
  ValueRef style = input.style ? css_longhand::FontStyle::Make(*input.style) : keyword(V::kNormal);
  ValueRef variant_caps =
      input.variant_caps ? IdentValue(*input.variant_caps, {V::kNormal, V::kSmallCaps}) : keyword(V::kNormal);
  ValueRef weight = input.weight ? css_longhand::FontWeight::Make(*input.weight) : keyword(V::kNormal);
  ValueRef stretch = input.stretch ? css_longhand::FontStretch::MakeKeywordOnly(*input.stretch) : keyword(V::kNormal);
  ValueRef size = css_longhand::FontSize::Make(input.size);
  ValueRef line_height = input.line_height ? css_longhand::LineHeight::Make(*input.line_height) : keyword(V::kNormal);
  ValueRef family = css_longhand::FontFamily::Make(input.family);
  if (!style || !variant_caps || !weight || !stretch || !size || !line_height || !family) return false;

  using enum CSSPropertyID;
  const auto add = [&properties](CSSPropertyID property, ValueRef value) {
    properties.push_back(CSSPropertyValue{property, std::move(value)});
  };
  add(kFontStyle, std::move(style));
  add(kFontVariantCaps, std::move(variant_caps));
  // The reset-only longhands.
  add(kFontVariantLigatures, keyword(V::kNormal));
  add(kFontVariantNumeric, keyword(V::kNormal));
  add(kFontVariantEastAsian, keyword(V::kNormal));
  add(kFontVariantAlternates, keyword(V::kNormal));
  // RuntimeEnabledFeatures::CSSFontSizeAdjustEnabled() (stable).
  add(kFontSizeAdjust, keyword(V::kNone));
  add(kFontKerning, keyword(V::kAuto));
  add(kFontOpticalSizing, keyword(V::kAuto));
  add(kFontFeatureSettings, keyword(V::kNormal));
  add(kFontVariationSettings, keyword(V::kNormal));
  add(kFontVariantPosition, keyword(V::kNormal));
  add(kFontVariantEmoji, keyword(V::kNormal));
  add(kFontWeight, std::move(weight));
  add(kFontStretch, std::move(stretch));
  add(kFontSize, std::move(size));
  add(kLineHeight, std::move(line_height));
  add(kFontFamily, std::move(family));
  return true;
}

bool FontSynthesis::Make(const Input& input, CSSPropertyValues& properties) {
  Kinds kinds;
  if (const auto* id = std::get_if<CSSValueID>(&input)) {
    if (*id != V::kNone) return false;
  } else {
    kinds = std::get<Kinds>(input);
    if (!kinds.weight && !kinds.style && !kinds.small_caps) return false;
  }
  const auto value = [](bool allowed) -> ValueRef {
    return CSSIdentifierValue::Create(allowed ? V::kAuto : V::kNone);
  };
  properties.push_back(CSSPropertyValue{CSSPropertyID::kFontSynthesisWeight, value(kinds.weight)});
  properties.push_back(CSSPropertyValue{CSSPropertyID::kFontSynthesisStyle, value(kinds.style)});
  properties.push_back(CSSPropertyValue{CSSPropertyID::kFontSynthesisSmallCaps, value(kinds.small_caps)});
  return true;
}

bool FontVariant::Make(const Input& input, CSSPropertyValues& properties) {
  using enum CSSPropertyID;
  const auto normal = []() -> ValueRef {
    return CSSIdentifierValue::Create(V::kNormal);
  };
  if (const auto* id = std::get_if<CSSValueID>(&input)) {
    ValueRef ligatures = IdentValue(*id, {V::kNormal, V::kNone});
    if (!ligatures) return false;
    properties.push_back(CSSPropertyValue{kFontVariantLigatures, std::move(ligatures)});
    properties.push_back(CSSPropertyValue{kFontVariantCaps, normal()});
    properties.push_back(CSSPropertyValue{kFontVariantNumeric, normal()});
    properties.push_back(CSSPropertyValue{kFontVariantEastAsian, normal()});
    properties.push_back(CSSPropertyValue{kFontVariantAlternates, normal()});
    properties.push_back(CSSPropertyValue{kFontVariantPosition, normal()});
    properties.push_back(CSSPropertyValue{kFontVariantEmoji, normal()});
    return true;
  }
  const Components& components = std::get<Components>(input);
  if (components.ligatures.empty() && !components.caps && !components.alternates && components.numeric.empty() &&
      components.east_asian.empty() && !components.position && !components.emoji)
    return false;
  // Each parser's FinalizeValue(): normal when nothing was read.
  ValueRef ligatures = components.ligatures.empty() ? normal() : LigaturesList(components.ligatures);
  ValueRef numeric = components.numeric.empty() ? normal() : NumericList(components.numeric);
  ValueRef east_asian = components.east_asian.empty() ? normal() : EastAsianList(components.east_asian);
  ValueRef caps = components.caps ? IdentValue(*components.caps, {V::kSmallCaps, V::kAllSmallCaps, V::kPetiteCaps,
                                                                  V::kAllPetiteCaps, V::kUnicase, V::kTitlingCaps})
                                  : normal();
  ValueRef alternates = components.alternates ? AlternatesList(*components.alternates) : normal();
  ValueRef position = components.position ? IdentValue(*components.position, {V::kSub, V::kSuper}) : normal();
  ValueRef emoji = components.emoji ? IdentValue(*components.emoji, {V::kText, V::kEmoji, V::kUnicode}) : normal();
  if (!ligatures || !numeric || !east_asian || !caps || !alternates || !position || !emoji) return false;
  properties.push_back(CSSPropertyValue{kFontVariantLigatures, std::move(ligatures)});
  properties.push_back(CSSPropertyValue{kFontVariantNumeric, std::move(numeric)});
  properties.push_back(CSSPropertyValue{kFontVariantEastAsian, std::move(east_asian)});
  properties.push_back(CSSPropertyValue{kFontVariantCaps, std::move(caps)});
  properties.push_back(CSSPropertyValue{kFontVariantAlternates, std::move(alternates)});
  properties.push_back(CSSPropertyValue{kFontVariantPosition, std::move(position)});
  properties.push_back(CSSPropertyValue{kFontVariantEmoji, std::move(emoji)});
  return true;
}

bool TextDecoration::Make(const Input& input, CSSPropertyValues& properties) {
  return GreedyShorthand(
      {{CSSPropertyID::kTextDecorationLine, Component(input.line, css_longhand::TextDecorationLine::Make)},
       {CSSPropertyID::kTextDecorationThickness,
        Component(input.thickness, css_longhand::TextDecorationThickness::Make)},
       {CSSPropertyID::kTextDecorationStyle, Component(input.style, css_longhand::TextDecorationStyle::Make)},
       {CSSPropertyID::kTextDecorationColor, Component(input.color, css_longhand::TextDecorationColor::Make)}},
      properties);
}

bool TextEmphasis::Make(const Input& input, CSSPropertyValues& properties) {
  return GreedyShorthand(
      {{CSSPropertyID::kTextEmphasisStyle, Component(input.style, css_longhand::TextEmphasisStyle::Make)},
       {CSSPropertyID::kTextEmphasisColor, Component(input.color, css_longhand::TextEmphasisColor::Make)}},
      properties);
}

bool TextWrap::Make(const Input& input, CSSPropertyValues& properties) {
  return GreedyShorthand({{CSSPropertyID::kTextWrapMode, Component(input.mode, css_longhand::TextWrapMode::Make)},
                          {CSSPropertyID::kTextWrapStyle, Component(input.style, css_longhand::TextWrapStyle::Make)}},
                         properties);
}

bool WebkitTextStroke::Make(const Input& input, CSSPropertyValues& properties) {
  return GreedyShorthand(
      {{CSSPropertyID::kWebkitTextStrokeWidth, Component(input.width, css_longhand::WebkitTextStrokeWidth::Make)},
       {CSSPropertyID::kWebkitTextStrokeColor, Component(input.color, css_longhand::WebkitTextStrokeColor::Make)}},
      properties);
}

bool WhiteSpace::Make(const Input& input, CSSPropertyValues& properties) {
  std::optional<CSSValueID> predefined;
  if (const auto* id = std::get_if<CSSValueID>(&input)) {
    predefined = *id;
  } else {
    const Longhands& longhands = std::get<Longhands>(input);
    // A lone break-spaces or nowrap is read as the predefined keyword.
    if (!longhands.wrap_mode && longhands.collapse == V::kBreakSpaces) predefined = *longhands.collapse;
    if (!longhands.collapse && longhands.wrap_mode == V::kNowrap) predefined = *longhands.wrap_mode;
    if (!predefined) {
      return GreedyShorthand(
          {{CSSPropertyID::kWhiteSpaceCollapse,
            Component(longhands.collapse, css_longhand::WhiteSpaceCollapse::Make)},
           {CSSPropertyID::kTextWrapMode, Component(longhands.wrap_mode, css_longhand::TextWrapMode::Make)}},
          properties);
    }
  }
  const std::optional<EWhiteSpace> whitespace = PredefinedWhiteSpace(*predefined);
  if (!whitespace) return false;
  properties.push_back(CSSPropertyValue{CSSPropertyID::kWhiteSpaceCollapse,
                                        CSSIdentifierValue::Create(PlatformEnumToCSSValueID(ToWhiteSpaceCollapse(*whitespace)))});
  properties.push_back(CSSPropertyValue{CSSPropertyID::kTextWrapMode,
                                        CSSIdentifierValue::Create(PlatformEnumToCSSValueID(ToTextWrapMode(*whitespace)))});
  return true;
}

} // namespace css_shorthand

} // namespace bkfont
