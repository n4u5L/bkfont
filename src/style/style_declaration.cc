// Adapted from core/css/css_property_value_set.cc (MutableCSSPropertyValueSet)
// and the shorthand parsers in core/css/properties/shorthands/
// shorthands_custom.cc.
#include "style_declaration.h"

#include <algorithm>
#include <cmath>

#include "base/memory/values_equivalent.h"
#include "style/css_color.h"
#include "style/css_font_values.h"
#include "style/css_identifier_value.h"
#include "style/css_inherited_value.h"
#include "style/css_initial_value.h"
#include "style/css_numeric_literal_value.h"
#include "style/css_property_validation.h"
#include "style/css_shadow_value.h"
#include "style/css_string_value.h"
#include "style/css_unset_value.h"
#include "style/css_value_id_mappings.h"
#include "style/css_value_list.h"

namespace bkfont {
namespace {

using UnitType = CSSPrimitiveValue::UnitType;

std::shared_ptr<const CSSValue> CreateWideKeywordValue(CSSWideKeyword keyword) {
  switch (keyword) {
    case CSSWideKeyword::kInitial: return CSSInitialValue::Create();
    case CSSWideKeyword::kInherit: return CSSInheritedValue::Create();
    case CSSWideKeyword::kUnset: return cssvalue::CSSUnsetValue::Create();
  }
  return nullptr;
}

std::shared_ptr<const CSSValue> Ident(CSSValueID id) {
  return CSSIdentifierValue::Create(id);
}

std::shared_ptr<const CSSPrimitiveValue> Primitive(CSSLength length) {
  return CSSNumericLiteralValue::Create(length.value, length.unit);
}

bool BelongsTo(CSSPropertyID shorthand, CSSPropertyID longhand) {
  const auto longhands = GetCSSPropertyMetadata(shorthand)->longhands;
  return std::find(longhands.begin(), longhands.end(), longhand) != longhands.end();
}

// The value ParseShorthand() gives a component that is not specified.
std::shared_ptr<const CSSValue> OmittedValue(CSSPropertyID shorthand, CSSPropertyID longhand) {
  using enum CSSPropertyID;
  switch (shorthand) {
    case kFont:
      // ConsumeFont(): the reset-only subproperties and the omitted optional
      // components are explicit keywords, not implicit initial values.
      switch (longhand) {
        case kFontSizeAdjust: return Ident(CSSValueID::kNone);
        case kFontKerning:
        case kFontOpticalSizing: return Ident(CSSValueID::kAuto);
        case kFontSize:
        case kFontFamily: return nullptr; // Required.
        default: return Ident(CSSValueID::kNormal);
      }
    case kFontVariant:
      // ConsumeFontVariantShorthand().
      return Ident(CSSValueID::kNormal);
    case kFontSynthesis:
      // FontSynthesis::ParseShorthand(): a component that is not listed is
      // 'none'.
      return Ident(CSSValueID::kNone);
    default:
      // ConsumeShorthandGreedilyViaLonghands() and friends: the longhand's
      // initial value.
      return CSSInitialValue::Create();
  }
}

// Components the shorthand syntax cannot specify, or restricts further than
// the longhand does.
bool IsAllowedComponent(CSSPropertyID shorthand, CSSPropertyID longhand, const CSSValue& value) {
  using enum CSSPropertyID;
  if (shorthand == kFont) {
    switch (longhand) {
      case kFontVariantCaps: {
        // <font-variant-css2>: normal | small-caps.
        const auto* ident = DynamicTo<CSSIdentifierValue>(value);
        return ident && (ident->GetValueID() == CSSValueID::kNormal || ident->GetValueID() == CSSValueID::kSmallCaps);
      }
      case kFontStretch:
        // <font-stretch-css3>: keywords only.
        return value.IsIdentifierValue();
      case kFontStyle:
      case kFontWeight:
      case kFontSize:
      case kLineHeight:
      case kFontFamily: return true;
      default:
        // Reset-only subproperties.
        return false;
    }
  }
  if (shorthand == kFontSynthesis) {
    const auto* ident = DynamicTo<CSSIdentifierValue>(value);
    return ident && (ident->GetValueID() == CSSValueID::kAuto || ident->GetValueID() == CSSValueID::kNone);
  }
  return true;
}

} // namespace

CSSLineHeight CSSLineHeight::Normal() {
  return CSSLineHeight(CSSIdentifierValue::Create(CSSValueID::kNormal));
}
CSSLineHeight CSSLineHeight::Number(double value) {
  return CSSLineHeight(CSSNumericLiteralValue::Create(value, UnitType::kNumber));
}
CSSLineHeight::CSSLineHeight(CSSLength value) : value_(CSSNumericLiteralValue::Create(value.value, value.unit)) {}
CSSLineHeight::CSSLineHeight(CSSCalc value) : value_(CSSMathFunctionValue::Create(std::move(value.terms))) {}

bool StyleDeclaration::Entry::operator==(const Entry& other) const {
  return property == other.property && base::ValuesEquivalent(value, other.value);
}

bool StyleDeclaration::IsValidValue(CSSPropertyID id, const CSSValue& value) {
  return IsValidLonghandValue(ResolveCSSPropertyID(id), value);
}

std::span<const StyleDeclaration::Entry> StyleDeclaration::Entries() const {
  return entries_ ? std::span<const Entry>(entries_->data(), entries_->size()) : std::span<const Entry>();
}
StyleDeclaration::EntryVector& StyleDeclaration::Access() {
  if (!entries_) entries_ = std::make_shared<EntryVector>();
  else if (entries_.use_count() != 1) entries_ = std::make_shared<EntryVector>(*entries_);
  return *entries_;
}
int StyleDeclaration::FindPropertyIndex(CSSPropertyID id) const {
  const auto entries = Entries();
  for (size_t i = 0; i < entries.size(); ++i)
    if (entries[i].property == id) return static_cast<int>(i);
  return -1;
}
int StyleDeclaration::FindInsertionPointForID(CSSPropertyID id) {
  const int to_replace = FindPropertyIndex(id);
  if (to_replace < 0) return -1;
  if (!GetCSSPropertyMetadata(id)->logical_property_group.empty()) {
    const auto entries = Entries();
    for (int n = static_cast<int>(entries.size()) - 1; n > to_replace; --n) {
      if (IsInSameLogicalPropertyGroupWithDifferentMappingLogic(id, entries[n].property)) {
        Access().EraseAt(static_cast<wtf_size_t>(to_replace));
        return -1;
      }
    }
  }
  return to_replace;
}
const CSSValue* StyleDeclaration::Get(CSSPropertyID id) const {
  const int index = FindPropertyIndex(ResolveCSSPropertyID(id));
  return index < 0 ? nullptr : Entries()[index].value.get();
}

bool StyleDeclaration::SetLonghand(CSSPropertyID id, std::shared_ptr<const CSSValue> value) {
  Entry entry{id, WithParsedCalcRanges(id, std::move(value))};
  const int to_replace = FindInsertionPointForID(id);
  if (to_replace < 0) {
    Access().push_back(std::move(entry));
  } else if (!(Entries()[to_replace] == entry)) {
    Access()[static_cast<wtf_size_t>(to_replace)] = std::move(entry);
  }
  return true;
}

bool StyleDeclaration::Set(CSSPropertyID id, std::shared_ptr<const CSSValue> value) {
  id = ResolveCSSPropertyID(id);
  if (!value || !GetCSSPropertyMetadata(id)) return false;
  if (IsShorthand(id)) {
    if (value->IsCSSWideKeyword()) {
      // A CSS-wide keyword applies to every longhand of the shorthand.
      for (CSSPropertyID longhand : GetCSSPropertyMetadata(id)->longhands) (void)SetLonghand(longhand, value);
      return true;
    }
    // WhiteSpace::ParseShorthand: the single keywords.
    if (id == CSSPropertyID::kWhiteSpace) {
      const auto* keyword = DynamicTo<CSSIdentifierValue>(*value);
      if (!keyword) return false;
      switch (keyword->GetValueID()) {
        case CSSValueID::kNormal: return SetWhiteSpace(EWhiteSpace::kNormal);
        case CSSValueID::kNowrap: return SetWhiteSpace(EWhiteSpace::kNowrap);
        case CSSValueID::kPre: return SetWhiteSpace(EWhiteSpace::kPre);
        case CSSValueID::kPreLine: return SetWhiteSpace(EWhiteSpace::kPreLine);
        case CSSValueID::kPreWrap: return SetWhiteSpace(EWhiteSpace::kPreWrap);
        case CSSValueID::kBreakSpaces: return SetWhiteSpace(EWhiteSpace::kBreakSpaces);
        case CSSValueID::kCollapse:
        case CSSValueID::kWrap: return SetWhiteSpace(EWhiteSpace::kNormal);
        case CSSValueID::kPreserve: return SetWhiteSpace(EWhiteSpace::kPreWrap);
        case CSSValueID::kPreserveBreaks: return SetWhiteSpace(EWhiteSpace::kPreLine);
        default: return false;
      }
    }
    // font-variant: none sets font-variant-ligatures: none.
    if (id == CSSPropertyID::kFontVariant) {
      const auto* keyword = DynamicTo<CSSIdentifierValue>(*value);
      if (!keyword) return false;
      if (keyword->GetValueID() == CSSValueID::kNormal) return SetShorthand(id, {});
      if (keyword->GetValueID() == CSSValueID::kNone) {
        const Entry ligatures{CSSPropertyID::kFontVariantLigatures, Ident(CSSValueID::kNone)};
        return SetShorthand(id, std::span<const Entry>(&ligatures, 1));
      }
      return false;
    }
    // font-synthesis: none.
    if (id == CSSPropertyID::kFontSynthesis) {
      const auto* keyword = DynamicTo<CSSIdentifierValue>(*value);
      if (!keyword || keyword->GetValueID() != CSSValueID::kNone) return false;
      return SetShorthand(id, {});
    }
    return false;
  }
  if (!IsValidLonghandValue(id, *value)) return false;
  return SetLonghand(id, std::move(value));
}

bool StyleDeclaration::SetCSSWideKeyword(CSSPropertyID id, CSSWideKeyword keyword) {
  return Set(id, CreateWideKeywordValue(keyword));
}

bool StyleDeclaration::SetShorthand(CSSPropertyID shorthand, std::span<const Entry> longhands) {
  shorthand = ResolveCSSPropertyID(shorthand);
  if (!IsShorthand(shorthand)) return false;
  Vector<std::shared_ptr<const CSSValue>> values;
  for (CSSPropertyID longhand : GetCSSPropertyMetadata(shorthand)->longhands) {
    std::shared_ptr<const CSSValue> value;
    for (const Entry& entry : longhands)
      if (ResolveCSSPropertyID(entry.property) == longhand) value = entry.value;
    if (value) {
      // CSS-wide keywords cannot be components of a shorthand value.
      if (value->IsCSSWideKeyword() || !IsValidLonghandValue(longhand, *value) ||
          !IsAllowedComponent(shorthand, longhand, *value))
        return false;
    } else {
      value = OmittedValue(shorthand, longhand);
      if (!value) return false;
    }
    values.push_back(std::move(value));
  }
  for (const Entry& entry : longhands)
    if (!BelongsTo(shorthand, ResolveCSSPropertyID(entry.property))) return false;
  const auto ids = GetCSSPropertyMetadata(shorthand)->longhands;
  for (size_t i = 0; i < ids.size(); ++i) (void)SetLonghand(ids[i], std::move(values[static_cast<wtf_size_t>(i)]));
  return true;
}

bool StyleDeclaration::Remove(CSSPropertyID id) {
  id = ResolveCSSPropertyID(id);
  if (IsShorthand(id)) {
    bool removed = false;
    for (CSSPropertyID longhand : GetCSSPropertyMetadata(id)->longhands) removed |= Remove(longhand);
    return removed;
  }
  const int index = FindPropertyIndex(id);
  if (index < 0) return false;
  Access().EraseAt(static_cast<wtf_size_t>(index));
  return true;
}
void StyleDeclaration::Merge(const StyleDeclaration& other) {
  const StyleDeclaration snapshot(other);
  for (const auto& entry : snapshot.Entries()) (void)Set(entry.property, entry.value);
}
bool StyleDeclaration::operator==(const StyleDeclaration& other) const {
  return entries_ == other.entries_ || std::ranges::equal(Entries(), other.Entries());
}

bool StyleDeclaration::SetKeyword(CSSPropertyID id, CSSValueID value) {
  return Set(id, CSSIdentifierValue::Create(value));
}
bool StyleDeclaration::SetKeywordList(CSSPropertyID id, std::span<const CSSValueID> keywords) {
  CSSValueList::Values items;
  for (CSSValueID keyword : keywords) items.push_back(CSSIdentifierValue::Create(keyword));
  return Set(id, CSSValueList::CreateSpaceSeparated(std::move(items)));
}
bool StyleDeclaration::SetNumber(CSSPropertyID id, double value) {
  // Integral numbers are <integer> tokens.
  const bool integer = std::isfinite(value) && value == std::trunc(value);
  return Set(id, CSSNumericLiteralValue::Create(value, integer ? UnitType::kInteger : UnitType::kNumber)) ||
         (integer && Set(id, CSSNumericLiteralValue::Create(value, UnitType::kNumber)));
}
bool StyleDeclaration::SetLength(CSSPropertyID id, CSSLength value) {
  // text-indent takes its <length-percentage> in a list.
  if (ResolveCSSPropertyID(id) == CSSPropertyID::kTextIndent)
    return Set(id, CSSValueList::CreateSpaceSeparated({Primitive(value)}));
  return Set(id, Primitive(value));
}
bool StyleDeclaration::SetCalc(CSSPropertyID id, CSSCalc value) {
  auto calc = CSSMathFunctionValue::Create(std::move(value.terms));
  if (ResolveCSSPropertyID(id) == CSSPropertyID::kTextIndent)
    return Set(id, CSSValueList::CreateSpaceSeparated({std::move(calc)}));
  return Set(id, std::move(calc));
}
bool StyleDeclaration::SetString(CSSPropertyID id, const String& value) {
  return Set(id, CSSStringValue::Create(value));
}

bool StyleDeclaration::SetColor(StyleColorValue value) {
  return Set(CSSPropertyID::kColor, value.ToCSSValue());
}
bool StyleDeclaration::SetTextFillColor(StyleColorValue value) {
  return Set(CSSPropertyID::kWebkitTextFillColor, value.ToCSSValue());
}
bool StyleDeclaration::SetTextDecorationColor(StyleColorValue value) {
  return Set(CSSPropertyID::kTextDecorationColor, value.ToCSSValue());
}
bool StyleDeclaration::SetTextEmphasisColor(StyleColorValue value) {
  return Set(CSSPropertyID::kTextEmphasisColor, value.ToCSSValue());
}
bool StyleDeclaration::SetTextStrokeColor(StyleColorValue value) {
  return Set(CSSPropertyID::kWebkitTextStrokeColor, value.ToCSSValue());
}
bool StyleDeclaration::SetVisibility(EVisibility value) {
  return SetKeyword(CSSPropertyID::kVisibility, PlatformEnumToCSSValueID(value));
}
bool StyleDeclaration::SetLineHeight(const CSSLineHeight& value) {
  return Set(CSSPropertyID::kLineHeight, value.Value());
}
bool StyleDeclaration::SetLetterSpacing(CSSLength value) {
  return SetLength(CSSPropertyID::kLetterSpacing, value);
}
bool StyleDeclaration::SetLetterSpacing(CSSCalc value) {
  return SetCalc(CSSPropertyID::kLetterSpacing, std::move(value));
}
bool StyleDeclaration::SetLetterSpacingNormal() {
  return SetKeyword(CSSPropertyID::kLetterSpacing, CSSValueID::kNormal);
}
bool StyleDeclaration::SetWordSpacing(CSSLength value) {
  return SetLength(CSSPropertyID::kWordSpacing, value);
}
bool StyleDeclaration::SetWordSpacing(CSSCalc value) {
  return SetCalc(CSSPropertyID::kWordSpacing, std::move(value));
}
bool StyleDeclaration::SetWordSpacingNormal() {
  return SetKeyword(CSSPropertyID::kWordSpacing, CSSValueID::kNormal);
}
bool StyleDeclaration::SetTabSize(double spaces) {
  return Set(CSSPropertyID::kTabSize, CSSNumericLiteralValue::Create(spaces, UnitType::kNumber));
}
bool StyleDeclaration::SetTabSize(CSSLength value) {
  return SetLength(CSSPropertyID::kTabSize, value);
}
bool StyleDeclaration::SetTabSize(CSSCalc value) {
  return SetCalc(CSSPropertyID::kTabSize, std::move(value));
}
bool StyleDeclaration::SetWhiteSpaceCollapse(WhiteSpaceCollapse value) {
  return SetKeyword(CSSPropertyID::kWhiteSpaceCollapse, PlatformEnumToCSSValueID(value));
}
bool StyleDeclaration::SetTextWrapMode(TextWrapMode value) {
  return SetKeyword(CSSPropertyID::kTextWrapMode, PlatformEnumToCSSValueID(value));
}
bool StyleDeclaration::SetWhiteSpace(EWhiteSpace value) {
  // Like ToWhiteSpace(), accept all valid pairs, including the two pairs that
  // have no predefined keyword (preserve-breaks/break-spaces with nowrap).
  return SetWhiteSpace(ToWhiteSpaceCollapse(value), ToTextWrapMode(value));
}
bool StyleDeclaration::SetWhiteSpace(WhiteSpaceCollapse collapse, TextWrapMode wrap) {
  const Entry entries[] = {
      {CSSPropertyID::kWhiteSpaceCollapse, Ident(PlatformEnumToCSSValueID(collapse))},
      {CSSPropertyID::kTextWrapMode, Ident(PlatformEnumToCSSValueID(wrap))},
  };
  return SetShorthand(CSSPropertyID::kWhiteSpace, entries);
}

bool StyleDeclaration::SetFontFamily(std::span<const CSSFontFamilyName> families) {
  CSSValueList::Values items;
  for (const CSSFontFamilyName& family : families) {
    if (family.generic != CSSValueID::kInvalid) items.push_back(Ident(family.generic));
    else items.push_back(CSSFontFamilyValue::Create(family.name));
  }
  return Set(CSSPropertyID::kFontFamily, CSSValueList::CreateCommaSeparated(std::move(items)));
}

bool StyleDeclaration::SetFontFeatureSettings(std::span<const std::pair<AtomicString, int>> features) {
  if (features.empty()) return SetKeyword(CSSPropertyID::kFontFeatureSettings, CSSValueID::kNormal);
  CSSValueList::Values items;
  for (const auto& [tag, value] : features)
    items.push_back(cssvalue::CSSFontFeatureValue::Create(tag, CSSNumericLiteralValue::Create(value, UnitType::kInteger)));
  return Set(CSSPropertyID::kFontFeatureSettings, CSSValueList::CreateCommaSeparated(std::move(items)));
}

bool StyleDeclaration::SetFontVariationSettings(std::span<const std::pair<AtomicString, double>> axes) {
  if (axes.empty()) return SetKeyword(CSSPropertyID::kFontVariationSettings, CSSValueID::kNormal);
  CSSValueList::Values items;
  for (const auto& [tag, value] : axes)
    items.push_back(cssvalue::CSSFontVariationValue::Create(tag, CSSNumericLiteralValue::Create(value, UnitType::kNumber)));
  return Set(CSSPropertyID::kFontVariationSettings, CSSValueList::CreateCommaSeparated(std::move(items)));
}

bool StyleDeclaration::SetFontStyleOblique(CSSLength angle) {
  auto literal = CSSNumericLiteralValue::Create(angle.value, angle.unit);
  if (!literal->IsAngle()) return false;
  if (literal->DoubleValue() == 0.0) return SetKeyword(CSSPropertyID::kFontStyle, CSSValueID::kNormal);
  return Set(CSSPropertyID::kFontStyle,
             cssvalue::CSSFontStyleRangeValue::Create(CSSIdentifierValue::Create(CSSValueID::kOblique),
                                                      CSSValueList::CreateSpaceSeparated({std::move(literal)})));
}

bool StyleDeclaration::SetFontStyleOblique(CSSCalc angle) {
  return Set(CSSPropertyID::kFontStyle,
             cssvalue::CSSFontStyleRangeValue::Create(
                 CSSIdentifierValue::Create(CSSValueID::kOblique),
                 CSSValueList::CreateSpaceSeparated({CSSMathFunctionValue::Create(std::move(angle.terms))})));
}

bool StyleDeclaration::SetTextShadow(std::span<const CSSTextShadow> shadows) {
  if (shadows.empty()) return SetKeyword(CSSPropertyID::kTextShadow, CSSValueID::kNone);
  CSSValueList::Values items;
  for (const CSSTextShadow& shadow : shadows) {
    items.push_back(CSSShadowValue::Create(Primitive(shadow.x), Primitive(shadow.y),
                                           shadow.blur ? Primitive(*shadow.blur) : nullptr, nullptr, nullptr,
                                           shadow.color ? shadow.color->ToCSSValue() : nullptr));
  }
  return Set(CSSPropertyID::kTextShadow, CSSValueList::CreateCommaSeparated(std::move(items)));
}

bool StyleDeclaration::SetTextDecorationLine(TextDecorationLine lines) {
  if (lines == TextDecorationLine::kNone) return SetKeyword(CSSPropertyID::kTextDecorationLine, CSSValueID::kNone);
  Vector<CSSValueID> keywords;
  const std::pair<TextDecorationLine, CSSValueID> order[] = {
      {TextDecorationLine::kUnderline, CSSValueID::kUnderline},
      {TextDecorationLine::kOverline, CSSValueID::kOverline},
      {TextDecorationLine::kLineThrough, CSSValueID::kLineThrough},
      {TextDecorationLine::kBlink, CSSValueID::kBlink},
      {TextDecorationLine::kSpellingError, CSSValueID::kSpellingError},
      {TextDecorationLine::kGrammarError, CSSValueID::kGrammarError},
  };
  for (const auto& [flag, keyword] : order)
    if ((lines & flag) != TextDecorationLine::kNone) keywords.push_back(keyword);
  return SetKeywordList(CSSPropertyID::kTextDecorationLine, keywords);
}

bool StyleDeclaration::SetLocale(const String& locale) {
  return SetString(CSSPropertyID::kWebkitLocale, locale);
}

} // namespace bkfont
