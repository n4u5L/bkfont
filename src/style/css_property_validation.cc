// Adapted from the ParseSingleValue() functions of the ported longhands in
// core/css/properties/longhands/longhands_custom.cc, the helpers in
// core/css/properties/css_parsing_utils.cc and
// CSSParserFastPaths::IsValidKeywordPropertyAndValue().
#include "css_property_validation.h"

#include <cmath>
#include <initializer_list>
#include <limits>
#include <optional>

#include "style/css_color.h"
#include "style/css_font_values.h"
#include "style/css_identifier_value.h"
#include "style/css_math_function_value.h"
#include "style/css_numeric_literal_value.h"
#include "style/css_shadow_value.h"
#include "style/css_string_value.h"
#include "style/css_value_list.h"
#include "style/css_value_pair.h"

namespace bkfont {
namespace {

using UnitType = CSSPrimitiveValue::UnitType;
using ValueRange = CSSPrimitiveValue::ValueRange;
using Category = CSSMathFunctionValue::Category;

// css_parsing_utils.cc kMinObliqueValue / kMaxObliqueValue.
constexpr double kMinObliqueValue = -90.0;
constexpr double kMaxObliqueValue = 90.0;

bool IsIdent(const CSSValue& value, std::initializer_list<CSSValueID> ids) {
  const auto* ident = DynamicTo<CSSIdentifierValue>(value);
  if (!ident) return false;
  for (CSSValueID id : ids)
    if (ident->GetValueID() == id) return true;
  return false;
}

bool IsIdentInRange(const CSSValue& value, CSSValueID first, CSSValueID last) {
  const auto* ident = DynamicTo<CSSIdentifierValue>(value);
  return ident && ident->GetValueID() >= first && ident->GetValueID() <= last;
}

const CSSNumericLiteralValue* Literal(const CSSValue& value) {
  const auto* literal = DynamicTo<CSSNumericLiteralValue>(value);
  return literal && std::isfinite(literal->DoubleValue()) ? literal : nullptr;
}

std::optional<Category> CalcCategory(const CSSValue& value) {
  const auto* calc = DynamicTo<CSSMathFunctionValue>(value);
  if (!calc) return std::nullopt;
  for (const auto& term : calc->Terms())
    if (!std::isfinite(term.value)) return std::nullopt;
  return CSSMathFunctionValue::CategoryOf(calc->Terms());
}

// ConsumeLength(): a length literal (a negative one rejected for
// kNonNegative) or a length calc().
bool IsLength(const CSSValue& value, ValueRange range) {
  if (const auto* literal = Literal(value))
    return literal->IsLength() && (range == ValueRange::kAll || literal->DoubleValue() >= 0);
  return CalcCategory(value) == Category::kLength;
}

// ConsumeLengthOrPercent().
bool IsLengthOrPercent(const CSSValue& value, ValueRange range) {
  if (const auto* literal = Literal(value))
    return (literal->IsLength() || literal->IsPercentage()) &&
           (range == ValueRange::kAll || literal->DoubleValue() >= 0);
  const auto category = CalcCategory(value);
  return category == Category::kLength || category == Category::kPercent || category == Category::kLengthPercent;
}

// ConsumePercent().
bool IsPercent(const CSSValue& value, ValueRange range) {
  if (const auto* literal = Literal(value))
    return literal->IsPercentage() && (range == ValueRange::kAll || literal->DoubleValue() >= 0);
  return CalcCategory(value) == Category::kPercent;
}

// ConsumeNumber().
bool IsNumber(const CSSValue& value, ValueRange range) {
  if (const auto* literal = Literal(value))
    return literal->IsNumber() && (range == ValueRange::kAll || literal->DoubleValue() >= 0);
  return CalcCategory(value) == Category::kNumber;
}

// ConsumeInteger(): an integer token no less than `minimum`, or a number
// calc().
bool IsInteger(const CSSValue& value, double minimum) {
  if (const auto* literal = Literal(value)) return literal->IsInteger() && literal->DoubleValue() >= minimum;
  return CalcCategory(value) == Category::kNumber;
}

// ConsumeAngle() with limits for literals.
bool IsAngle(const CSSValue& value, double minimum, double maximum) {
  if (const auto* literal = Literal(value)) {
    if (!literal->IsAngle()) return false;
    const double degrees = literal->ComputeDegrees();
    return degrees >= minimum && degrees <= maximum;
  }
  return CalcCategory(value) == Category::kAngle;
}

// ConsumeColor() for the local color subset: an RGBA color or currentcolor.
bool IsColor(const CSSValue& value) {
  if (IsIdent(value, {CSSValueID::kCurrentcolor})) return true;
  if (const auto* color = DynamicTo<cssvalue::CSSColor>(value)) {
    const Color rgba = color->Value();
    return std::isfinite(rgba.Param0()) && std::isfinite(rgba.Param1()) && std::isfinite(rgba.Param2()) &&
           std::isfinite(rgba.Alpha());
  }
  return false;
}

// A space-separated list of distinct keywords where each group may appear
// at most once (the FontVariant*Parser classes).
bool IsKeywordGroupList(const CSSValue& value, std::initializer_list<std::initializer_list<CSSValueID>> groups) {
  const auto* list = DynamicTo<CSSValueList>(value);
  if (!list || !list->IsSpaceSeparated() || !list->length()) return false;
  Vector<bool> used(static_cast<wtf_size_t>(groups.size()), false);
  for (const auto& item : *list) {
    const auto* ident = DynamicTo<CSSIdentifierValue>(*item);
    if (!ident) return false;
    wtf_size_t index = 0;
    bool found = false;
    for (const auto& group : groups) {
      for (CSSValueID id : group) {
        if (ident->GetValueID() == id) {
          found = true;
          break;
        }
      }
      if (found) break;
      ++index;
    }
    if (!found || used[index]) return false;
    used[index] = true;
  }
  return true;
}

// ConsumeFontSettingsTagAndValue(): a four-character tag in 0x20-0x7E.
bool IsFontTag(const AtomicString& tag) {
  if (tag.length() != 4) return false;
  for (unsigned i = 0; i < 4; ++i)
    if (tag[i] < 0x20 || tag[i] > 0x7E) return false;
  return true;
}

bool IsFontPalette(const CSSValue& value);

// ConsumePaletteMixFunction().
bool IsPaletteMix(const CSSValue& value) {
  const auto* mix = DynamicTo<cssvalue::CSSPaletteMixValue>(value);
  if (!mix || !IsFontPalette(mix->Palette1()) || !IsFontPalette(mix->Palette2())) return false;
  const auto valid_percentage = [](const CSSPrimitiveValue* percentage) {
    if (!percentage) return true;
    if (!IsPercent(*percentage, ValueRange::kAll)) return false;
    // Reject negative values and values > 100%, but not calc() values.
    const auto* literal = DynamicTo<CSSNumericLiteralValue>(percentage);
    return !literal || (literal->ComputePercentage() >= 0.0 && literal->ComputePercentage() <= 100.0);
  };
  if (!valid_percentage(mix->Percentage1()) || !valid_percentage(mix->Percentage2())) return false;
  // If both values are literally zero (and not calc()) reject.
  const auto* p1 = DynamicTo<CSSNumericLiteralValue>(mix->Percentage1());
  const auto* p2 = DynamicTo<CSSNumericLiteralValue>(mix->Percentage2());
  return !(p1 && p2 && p1->ComputePercentage() == 0.0 && p2->ComputePercentage() == 0.0);
}

// ConsumeFontPalette().
bool IsFontPalette(const CSSValue& value) {
  if (IsIdent(value, {CSSValueID::kNormal, CSSValueID::kLight, CSSValueID::kDark})) return true;
  if (const auto* ident = DynamicTo<CSSCustomIdentValue>(value)) {
    // ConsumeDashedIdent().
    const AtomicString& name = ident->Value();
    return name.length() > 2 && name[0] == '-' && name[1] == '-';
  }
  return IsPaletteMix(value);
}

bool IsFontFamily(const CSSValue& value) {
  const auto* list = DynamicTo<CSSValueList>(value);
  if (!list || !list->IsCommaSeparated() || !list->length()) return false;
  for (const auto& family : *list) {
    // ConsumeGenericFamily() or ConsumeFamilyName().
    if (!IsIdentInRange(*family, CSSValueID::kSerif, CSSValueID::kMath) && !family->IsFontFamilyValue())
      return false;
  }
  return true;
}

bool IsFontFeatureSettings(const CSSValue& value) {
  if (IsIdent(value, {CSSValueID::kNormal})) return true;
  const auto* list = DynamicTo<CSSValueList>(value);
  if (!list || !list->IsCommaSeparated() || !list->length()) return false;
  for (const auto& item : *list) {
    const auto* feature = DynamicTo<cssvalue::CSSFontFeatureValue>(*item);
    // <integer> | on | off; on/off and a bare tag are stored as numbers.
    if (!feature || !IsFontTag(feature->Tag()) ||
        !(IsInteger(feature->Value(), -std::numeric_limits<double>::max()) ||
          (Literal(feature->Value()) && Literal(feature->Value())->GetType() == UnitType::kNumber &&
           (Literal(feature->Value())->DoubleValue() == 0 || Literal(feature->Value())->DoubleValue() == 1))))
      return false;
  }
  return true;
}

bool IsFontVariationSettings(const CSSValue& value) {
  if (IsIdent(value, {CSSValueID::kNormal})) return true;
  const auto* list = DynamicTo<CSSValueList>(value);
  if (!list || !list->IsCommaSeparated() || !list->length()) return false;
  for (const auto& item : *list) {
    const auto* variation = DynamicTo<cssvalue::CSSFontVariationValue>(*item);
    if (!variation || !IsFontTag(variation->Tag()) || !variation->Value() ||
        !IsNumber(*variation->Value(), ValueRange::kAll))
      return false;
  }
  return true;
}

bool IsFontSize(const CSSValue& value) {
  // ConsumeFontSize(). 'math' resolves against math-depth, whose computed
  // value is always the initial one here.
  if (IsIdentInRange(value, CSSValueID::kXxSmall, CSSValueID::kWebkitXxxLarge) ||
      IsIdent(value, {CSSValueID::kMath}))
    return true;
  return IsLengthOrPercent(value, ValueRange::kNonNegative);
}

bool IsFontSizeAdjust(const CSSValue& value) {
  if (IsIdent(value, {CSSValueID::kNone, CSSValueID::kFromFont})) return true;
  if (IsNumber(value, ValueRange::kNonNegative)) return true;
  // The ex-height metric is implied and never stored in a pair.
  const auto* pair = DynamicTo<CSSValuePair>(value);
  return pair && pair->KeepIdenticalValues() &&
         IsIdent(pair->First(), {CSSValueID::kCapHeight, CSSValueID::kChWidth, CSSValueID::kIcWidth,
                                 CSSValueID::kIcHeight}) &&
         (IsNumber(pair->Second(), ValueRange::kNonNegative) || IsIdent(pair->Second(), {CSSValueID::kFromFont}));
}

bool IsFontStretchKeyword(const CSSValue& value) {
  return IsIdent(value, {CSSValueID::kNormal}) ||
         IsIdentInRange(value, CSSValueID::kUltraCondensed, CSSValueID::kUltraExpanded);
}

bool IsFontStyle(const CSSValue& value) {
  if (IsIdent(value, {CSSValueID::kNormal, CSSValueID::kItalic, CSSValueID::kOblique})) return true;
  const auto* range = DynamicTo<cssvalue::CSSFontStyleRangeValue>(value);
  if (!range || range->GetFontStyleValue()->GetValueID() != CSSValueID::kOblique) return false;
  const CSSValueList* angles = range->GetObliqueValues();
  if (!angles || angles->length() != 1 || !IsAngle(angles->First(), kMinObliqueValue, kMaxObliqueValue))
    return false;
  // FontStyleObliqueZeroDegreeAsNormal: a literal zero angle parses as
  // 'normal' instead.
  const auto* literal = DynamicTo<CSSNumericLiteralValue>(angles->First());
  return !literal || literal->ComputeDegrees() != 0.0;
}

bool IsFontWeight(const CSSValue& value) {
  if (IsIdentInRange(value, CSSValueID::kNormal, CSSValueID::kLighter)) return true;
  if (!IsNumber(value, ValueRange::kNonNegative)) return false;
  const auto* literal = DynamicTo<CSSNumericLiteralValue>(value);
  return !literal || (literal->DoubleValue() >= 1 && literal->DoubleValue() <= 1000);
}

bool IsFontVariantAlternates(const CSSValue& value) {
  if (IsIdent(value, {CSSValueID::kNormal})) return true;
  const auto* list = DynamicTo<CSSValueList>(value);
  if (!list || !list->IsSpaceSeparated() || !list->length()) return false;
  bool historical = false;
  Vector<CSSValueID> functions;
  for (const auto& item : *list) {
    if (IsIdent(*item, {CSSValueID::kHistoricalForms})) {
      if (historical) return false;
      historical = true;
      continue;
    }
    const auto* alternate = DynamicTo<cssvalue::CSSAlternateValue>(*item);
    if (!alternate) return false;
    const CSSValueID function = alternate->Function().FunctionType();
    if (functions.Contains(function)) return false;
    functions.push_back(function);
    const CSSValueList& aliases = alternate->Aliases();
    for (const auto& alias : aliases)
      if (!alias->IsCustomIdentValue()) return false;
    switch (function) {
      case CSSValueID::kStylistic:
      case CSSValueID::kSwash:
      case CSSValueID::kOrnaments:
      case CSSValueID::kAnnotation:
        if (aliases.length() != 1) return false;
        break;
      case CSSValueID::kStyleset:
      case CSSValueID::kCharacterVariant:
        if (!aliases.length()) return false;
        break;
      default: return false;
    }
  }
  return true;
}

bool IsHyphenateLimitChars(const CSSValue& value) {
  const auto* list = DynamicTo<CSSValueList>(value);
  if (!list || !list->IsSpaceSeparated() || !list->length() || list->length() > 3) return false;
  for (const auto& item : *list)
    if (!IsIdent(*item, {CSSValueID::kAuto}) && !IsInteger(*item, 1)) return false;
  return true;
}

bool IsTextDecorationLine(const CSSValue& value) {
  if (IsIdent(value, {CSSValueID::kNone})) return true;
  const auto* list = DynamicTo<CSSValueList>(value);
  if (!list || !list->IsSpaceSeparated() || !list->length()) return false;
  if (list->length() == 1 && IsIdent(list->First(), {CSSValueID::kSpellingError, CSSValueID::kGrammarError}))
    return true;
  // ConsumeTextDecorationLine() appends the lines in this order.
  const CSSValueID order[] = {CSSValueID::kUnderline, CSSValueID::kOverline, CSSValueID::kLineThrough,
                              CSSValueID::kBlink};
  size_t next = 0;
  for (const auto& item : *list) {
    const auto* ident = DynamicTo<CSSIdentifierValue>(*item);
    if (!ident) return false;
    while (next < std::size(order) && order[next] != ident->GetValueID()) ++next;
    if (next == std::size(order)) return false;
    ++next;
  }
  return true;
}

bool IsTextEmphasisPosition(const CSSValue& value) {
  const auto* list = DynamicTo<CSSValueList>(value);
  if (!list || !list->IsSpaceSeparated() || !list->length() || list->length() > 2) return false;
  // The over/under keyword first, then the optional left/right keyword.
  if (!IsIdent(list->First(), {CSSValueID::kOver, CSSValueID::kUnder})) return false;
  return list->length() == 1 || IsIdent(list->Item(1), {CSSValueID::kLeft, CSSValueID::kRight});
}

bool IsTextEmphasisStyle(const CSSValue& value) {
  if (IsIdent(value, {CSSValueID::kNone})) return true;
  if (value.IsStringValue()) return true;
  const auto is_fill = [](const CSSValue& v) { return IsIdent(v, {CSSValueID::kFilled, CSSValueID::kOpen}); };
  const auto is_shape = [](const CSSValue& v) {
    return IsIdent(v, {CSSValueID::kDot, CSSValueID::kCircle, CSSValueID::kDoubleCircle, CSSValueID::kTriangle,
                       CSSValueID::kSesame});
  };
  if (is_fill(value) || is_shape(value)) return true;
  const auto* list = DynamicTo<CSSValueList>(value);
  return list && list->IsSpaceSeparated() && list->length() == 2 && is_fill(list->First()) &&
         is_shape(list->Item(1));
}

bool IsTextShadow(const CSSValue& value) {
  if (IsIdent(value, {CSSValueID::kNone})) return true;
  const auto* list = DynamicTo<CSSValueList>(value);
  if (!list || !list->IsCommaSeparated() || !list->length()) return false;
  for (const auto& item : *list) {
    // ParseSingleShadow() with AllowInsetAndSpread::kForbid.
    const auto* shadow = DynamicTo<CSSShadowValue>(*item);
    if (!shadow || !shadow->x || !shadow->y || !IsLength(*shadow->x, ValueRange::kAll) ||
        !IsLength(*shadow->y, ValueRange::kAll) || shadow->spread || shadow->style)
      return false;
    if (shadow->blur && !IsLength(*shadow->blur, ValueRange::kNonNegative)) return false;
    if (shadow->color && !IsColor(*shadow->color)) return false;
  }
  return true;
}

bool IsTextUnderlinePosition(const CSSValue& value) {
  if (IsIdent(value, {CSSValueID::kAuto})) return true;
  const auto* list = DynamicTo<CSSValueList>(value);
  if (!list || !list->IsSpaceSeparated() || !list->length() || list->length() > 2) return false;
  // [from-font | under] first, then [left | right].
  const bool first_is_side = IsIdent(list->First(), {CSSValueID::kLeft, CSSValueID::kRight});
  if (!first_is_side && !IsIdent(list->First(), {CSSValueID::kFromFont, CSSValueID::kUnder})) return false;
  if (list->length() == 1) return true;
  return !first_is_side && IsIdent(list->Item(1), {CSSValueID::kLeft, CSSValueID::kRight});
}

bool IsLonghandValue(CSSPropertyID id, const CSSValue& value) {
  using enum CSSPropertyID;
  using V = CSSValueID;
  switch (id) {
    case kColor:
    case kWebkitTextFillColor:
    case kTextDecorationColor:
    case kTextEmphasisColor:
    case kWebkitTextStrokeColor: return IsColor(value);
    case kDirection: return IsIdent(value, {V::kLtr, V::kRtl});
    case kFontFamily: return IsFontFamily(value);
    case kFontFeatureSettings: return IsFontFeatureSettings(value);
    case kFontKerning: return IsIdent(value, {V::kAuto, V::kNormal, V::kNone});
    case kFontOpticalSizing: return IsIdent(value, {V::kAuto, V::kNone});
    case kFontPalette: return IsFontPalette(value);
    case kFontSize: return IsFontSize(value);
    case kFontSizeAdjust: return IsFontSizeAdjust(value);
    case kFontStretch: return IsFontStretchKeyword(value) || IsPercent(value, ValueRange::kNonNegative);
    case kFontStyle: return IsFontStyle(value);
    case kFontSynthesisSmallCaps:
    case kFontSynthesisStyle:
    case kFontSynthesisWeight: return IsIdent(value, {V::kAuto, V::kNone});
    case kFontVariantAlternates: return IsFontVariantAlternates(value);
    case kFontVariantCaps:
      return IsIdent(value, {V::kNormal, V::kSmallCaps, V::kAllSmallCaps, V::kPetiteCaps, V::kAllPetiteCaps,
                             V::kUnicase, V::kTitlingCaps});
    case kFontVariantEastAsian:
      return IsIdent(value, {V::kNormal}) ||
             IsKeywordGroupList(value, {{V::kJis78, V::kJis83, V::kJis90, V::kJis04, V::kSimplified, V::kTraditional},
                                        {V::kFullWidth, V::kProportionalWidth},
                                        {V::kRuby}});
    case kFontVariantEmoji: return IsIdent(value, {V::kNormal, V::kText, V::kEmoji, V::kUnicode});
    case kFontVariantLigatures:
      return IsIdent(value, {V::kNormal, V::kNone}) ||
             IsKeywordGroupList(value, {{V::kCommonLigatures, V::kNoCommonLigatures},
                                        {V::kDiscretionaryLigatures, V::kNoDiscretionaryLigatures},
                                        {V::kHistoricalLigatures, V::kNoHistoricalLigatures},
                                        {V::kContextual, V::kNoContextual}});
    case kFontVariantNumeric:
      return IsIdent(value, {V::kNormal}) ||
             IsKeywordGroupList(value, {{V::kLiningNums, V::kOldstyleNums},
                                        {V::kProportionalNums, V::kTabularNums},
                                        {V::kDiagonalFractions, V::kStackedFractions},
                                        {V::kOrdinal},
                                        {V::kSlashedZero}});
    case kFontVariantPosition: return IsIdent(value, {V::kNormal, V::kSub, V::kSuper});
    case kFontVariationSettings: return IsFontVariationSettings(value);
    case kFontWeight: return IsFontWeight(value);
    case kTextOrientation: return IsIdent(value, {V::kMixed, V::kUpright, V::kSideways, V::kSidewaysRight});
    case kTextRendering:
      return IsIdent(value, {V::kAuto, V::kOptimizespeed, V::kOptimizelegibility, V::kGeometricprecision});
    case kTextSpacingTrim: return IsIdent(value, {V::kNormal, V::kTrimStart, V::kSpaceAll, V::kSpaceFirst});
    case kWebkitFontSmoothing:
      return IsIdent(value, {V::kAuto, V::kNone, V::kAntialiased, V::kSubpixelAntialiased});
    case kWebkitLocale: return IsIdent(value, {V::kAuto}) || value.IsStringValue();
    case kWebkitTextOrientation:
      return IsIdent(value, {V::kSideways, V::kSidewaysRight, V::kVerticalRight, V::kUpright});
    case kWebkitWritingMode: return IsIdentInRange(value, V::kHorizontalTb, V::kVerticalLr);
    case kWritingMode:
      return IsIdent(value, {V::kHorizontalTb, V::kVerticalRl, V::kVerticalLr, V::kSidewaysRl, V::kSidewaysLr,
                             V::kLrTb, V::kRlTb, V::kTbRl, V::kLr, V::kRl, V::kTb});
    case kHyphenateCharacter: return IsIdent(value, {V::kAuto}) || value.IsStringValue();
    case kHyphenateLimitChars: return IsHyphenateLimitChars(value);
    case kHyphens:
      // USE_MINIKIN_HYPHENATION: all platforms hyphenate like Linux here.
      return IsIdent(value, {V::kAuto, V::kNone, V::kManual});
    case kLetterSpacing:
    case kWordSpacing:
      // ParseSpacing() without CSSLetterAndWordSpacingPercentage
      // (experimental upstream).
      return IsIdent(value, {V::kNormal}) || IsLength(value, ValueRange::kAll);
    case kLineBreak: return IsIdent(value, {V::kAuto, V::kLoose, V::kNormal, V::kStrict, V::kAnywhere});
    case kLineHeight:
      return IsIdent(value, {V::kNormal}) || IsNumber(value, ValueRange::kNonNegative) ||
             IsLengthOrPercent(value, ValueRange::kNonNegative);
    case kOverflowWrap: return IsIdent(value, {V::kNormal, V::kBreakWord, V::kAnywhere});
    case kTabSize: return IsNumber(value, ValueRange::kNonNegative) || IsLength(value, ValueRange::kNonNegative);
    case kTextAlign: return IsIdentInRange(value, V::kWebkitAuto, V::kInternalCenter) || IsIdent(value, {V::kStart, V::kEnd});
    case kTextAlignLast:
      return IsIdentInRange(value, V::kLeft, V::kJustify) || IsIdent(value, {V::kStart, V::kEnd, V::kAuto});
    case kTextAutospace: return IsIdent(value, {V::kNormal, V::kNoAutospace});
    case kTextCombineUpright: return IsIdent(value, {V::kNone, V::kAll});
    case kTextDecorationLine: return IsTextDecorationLine(value);
    case kTextDecorationSkipInk: return IsIdent(value, {V::kAuto, V::kNone});
    case kTextDecorationStyle: return IsIdent(value, {V::kSolid, V::kDouble, V::kDotted, V::kDashed, V::kWavy});
    case kTextDecorationThickness:
      return IsIdent(value, {V::kFromFont, V::kAuto}) || IsLengthOrPercent(value, ValueRange::kAll);
    case kTextEmphasisPosition: return IsTextEmphasisPosition(value);
    case kTextEmphasisStyle: return IsTextEmphasisStyle(value);
    case kTextIndent: {
      const auto* list = DynamicTo<CSSValueList>(value);
      return list && list->IsSpaceSeparated() && list->length() == 1 &&
             IsLengthOrPercent(list->First(), ValueRange::kAll);
    }
    case kTextShadow: return IsTextShadow(value);
    case kTextTransform: return IsIdentInRange(value, V::kCapitalize, V::kMathAuto) || IsIdent(value, {V::kNone});
    case kTextUnderlineOffset: return IsIdent(value, {V::kAuto}) || IsLengthOrPercent(value, ValueRange::kAll);
    case kTextUnderlinePosition: return IsTextUnderlinePosition(value);
    case kTextWrapMode: return IsIdent(value, {V::kWrap, V::kNowrap});
    case kTextWrapStyle: return IsIdent(value, {V::kAuto, V::kBalance, V::kPretty, V::kStable});
    case kUnicodeBidi:
      return IsIdent(value, {V::kNormal, V::kEmbed, V::kBidiOverride, V::kWebkitIsolate, V::kWebkitIsolateOverride,
                             V::kWebkitPlaintext, V::kIsolate, V::kIsolateOverride, V::kPlaintext});
    case kVerticalAlign:
      return IsIdentInRange(value, V::kBaseline, V::kWebkitBaselineMiddle) ||
             IsLengthOrPercent(value, ValueRange::kAll);
    case kVisibility: return IsIdent(value, {V::kVisible, V::kHidden, V::kCollapse});
    case kWebkitLineBreak:
      return IsIdent(value, {V::kAuto, V::kLoose, V::kNormal, V::kStrict, V::kAfterWhiteSpace});
    case kWebkitTextStrokeWidth:
      return IsIdent(value, {V::kThin, V::kMedium, V::kThick}) || IsLength(value, ValueRange::kNonNegative);
    case kWhiteSpaceCollapse:
      return IsIdent(value, {V::kCollapse, V::kPreserve, V::kPreserveBreaks, V::kBreakSpaces});
    case kWordBreak:
      return IsIdent(value, {V::kNormal, V::kBreakAll, V::kKeepAll, V::kBreakWord, V::kAutoPhrase});
    default: return false;
  }
}

std::shared_ptr<const CSSPrimitiveValue> WithRange(std::shared_ptr<const CSSPrimitiveValue> value, ValueRange range) {
  const auto* calc = DynamicTo<CSSMathFunctionValue>(value.get());
  if (!calc || calc->PermittedValueRange() == range) return value;
  return CSSMathFunctionValue::Create(calc->Terms(), range);
}

std::shared_ptr<const CSSValue> WithRange(std::shared_ptr<const CSSValue> value, ValueRange range) {
  if (!value || !value->IsMathFunctionValue()) return value;
  return WithRange(std::static_pointer_cast<const CSSPrimitiveValue>(value), range);
}

// Rebuilds a list whose items get `range`.
std::shared_ptr<const CSSValue> ListWithRange(const CSSValueList& list, ValueRange range) {
  CSSValueList::Values items;
  for (const auto& item : list) items.push_back(WithRange(item, range));
  return list.IsCommaSeparated() ? CSSValueList::CreateCommaSeparated(std::move(items))
                                 : CSSValueList::CreateSpaceSeparated(std::move(items));
}

} // namespace

bool IsValidLonghandValue(CSSPropertyID id, const CSSValue& value) {
  if (!IsLonghand(id)) return false;
  if (value.IsCSSWideKeyword()) return true;
  return IsLonghandValue(id, value);
}

std::shared_ptr<const CSSValue> WithParsedCalcRanges(CSSPropertyID id, std::shared_ptr<const CSSValue> value) {
  using enum CSSPropertyID;
  switch (id) {
    case kFontSize:
    case kLineHeight:
    case kTabSize:
    case kWebkitTextStrokeWidth:
    case kFontStretch:
    case kFontWeight:
    case kFontSizeAdjust:
      if (const auto* pair = DynamicTo<CSSValuePair>(value.get())) {
        if (!pair->Second().IsMathFunctionValue()) return value;
        const auto& calc = To<CSSMathFunctionValue>(pair->Second());
        return CSSValuePair::Create(CSSIdentifierValue::Create(To<CSSIdentifierValue>(pair->First()).GetValueID()),
                                    CSSMathFunctionValue::Create(calc.Terms(), ValueRange::kNonNegative),
                                    CSSValuePair::kKeepIdenticalValues);
      }
      return WithRange(std::move(value), ValueRange::kNonNegative);
    case kHyphenateLimitChars:
      if (const auto* list = DynamicTo<CSSValueList>(value.get())) return ListWithRange(*list, ValueRange::kPositiveInteger);
      return value;
    case kTextIndent:
      if (const auto* list = DynamicTo<CSSValueList>(value.get())) return ListWithRange(*list, ValueRange::kAll);
      return value;
    case kFontFeatureSettings:
    case kFontVariationSettings: {
      const auto* list = DynamicTo<CSSValueList>(value.get());
      if (!list) return value;
      CSSValueList::Values items;
      for (const auto& item : *list) {
        if (const auto* feature = DynamicTo<cssvalue::CSSFontFeatureValue>(*item)) {
          auto primitive = std::shared_ptr<const CSSPrimitiveValue>(item, &feature->Value());
          items.push_back(cssvalue::CSSFontFeatureValue::Create(feature->Tag(), WithRange(primitive, ValueRange::kInteger)));
        } else {
          const auto& variation = To<cssvalue::CSSFontVariationValue>(*item);
          auto primitive = std::shared_ptr<const CSSPrimitiveValue>(item, variation.Value());
          items.push_back(cssvalue::CSSFontVariationValue::Create(variation.Tag(), WithRange(primitive, ValueRange::kAll)));
        }
      }
      return CSSValueList::CreateCommaSeparated(std::move(items));
    }
    case kTextShadow: {
      const auto* list = DynamicTo<CSSValueList>(value.get());
      if (!list) return value;
      CSSValueList::Values items;
      for (const auto& item : *list) {
        const auto& shadow = To<CSSShadowValue>(*item);
        items.push_back(CSSShadowValue::Create(WithRange(shadow.x, ValueRange::kAll), WithRange(shadow.y, ValueRange::kAll),
                                               shadow.blur ? WithRange(shadow.blur, ValueRange::kNonNegative) : nullptr,
                                               nullptr, nullptr, shadow.color));
      }
      return CSSValueList::CreateCommaSeparated(std::move(items));
    }
    default: return WithRange(std::move(value), ValueRange::kAll);
  }
}

} // namespace bkfont
