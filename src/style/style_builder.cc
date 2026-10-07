// Adapted from core/css/resolver/style_builder.cc, the generated
// style_builder_functions.cc (templates/style_builder_functions.tmpl) and the
// custom ApplyInitial/ApplyInherit/ApplyValue functions in
// properties/longhands/longhands_custom.cc.
#include "style_builder.h"

#include <cassert>

#include "base/notreached.h"
#include "style/css_color.h"
#include "style/css_identifier_value.h"
#include "style/css_primitive_value.h"
#include "style/css_string_value.h"
#include "style/css_value_id_mappings.h"
#include "style/css_value_list.h"
#include "style/style_builder_converter.h"
#include "style/style_resolver_state.h"

namespace bkit {
namespace {

using enum CSSPropertyID;
using Converter = StyleBuilderConverter;

const CSSIdentifierValue& Ident(const CSSValue& value) {
  return To<CSSIdentifierValue>(value);
}

// Longhand::ApplyInitial().
void ApplyInitial(CSSPropertyID id, StyleResolverState& state) {
  ComputedStyleBuilder& builder = state.StyleBuilder();
  FontBuilder& font = state.GetFontBuilder();
  const ComputedStyle& initial = state.InitialStyle();
  switch (id) {
    case kColor:
      // InitialColorForColorScheme(): the host's initial color.
      builder.SetColor(state.Host().GetSettings().initial_color);
      break;
    case kDirection: builder.SetDirection(TextDirection::kLtr); break;
    case kFontFamily: font.SetFamilyDescription(FontBuilder::InitialFamilyDescription()); break;
    case kFontFeatureSettings: font.SetFeatureSettings(FontBuilder::InitialFeatureSettings()); break;
    case kFontKerning: font.SetKerning(FontBuilder::InitialKerning()); break;
    case kFontOpticalSizing: font.SetFontOpticalSizing(FontBuilder::InitialFontOpticalSizing()); break;
    case kFontPalette: font.SetFontPalette(FontBuilder::InitialFontPalette()); break;
    case kFontSize: font.SetSize(FontBuilder::InitialSize()); break;
    case kFontSizeAdjust: font.SetSizeAdjust(FontBuilder::InitialSizeAdjust()); break;
    case kFontStretch: font.SetStretch(FontBuilder::InitialStretch()); break;
    case kFontStyle: font.SetStyle(FontBuilder::InitialStyle()); break;
    case kFontSynthesisSmallCaps: font.SetFontSynthesisSmallCaps(FontBuilder::InitialFontSynthesisSmallCaps()); break;
    case kFontSynthesisStyle: font.SetFontSynthesisStyle(FontBuilder::InitialFontSynthesisStyle()); break;
    case kFontSynthesisWeight: font.SetFontSynthesisWeight(FontBuilder::InitialFontSynthesisWeight()); break;
    case kFontVariantAlternates: font.SetFontVariantAlternates(FontBuilder::InitialFontVariantAlternates()); break;
    case kFontVariantCaps: font.SetVariantCaps(FontBuilder::InitialVariantCaps()); break;
    case kFontVariantEastAsian: font.SetVariantEastAsian(FontBuilder::InitialVariantEastAsian()); break;
    case kFontVariantEmoji: font.SetVariantEmoji(FontBuilder::InitialVariantEmoji()); break;
    case kFontVariantLigatures: font.SetVariantLigatures(FontBuilder::InitialVariantLigatures()); break;
    case kFontVariantNumeric: font.SetVariantNumeric(FontBuilder::InitialVariantNumeric()); break;
    case kFontVariantPosition: font.SetVariantPosition(FontBuilder::InitialVariantPosition()); break;
    case kFontVariationSettings: font.SetVariationSettings(FontBuilder::InitialVariationSettings()); break;
    case kFontWeight: font.SetWeight(FontBuilder::InitialWeight()); break;
    case kTextOrientation: state.SetTextOrientation(ETextOrientation::kMixed); break;
    case kTextRendering: font.SetTextRendering(FontBuilder::InitialTextRendering()); break;
    case kTextSpacingTrim: font.SetTextSpacingTrim(FontBuilder::InitialTextSpacingTrim()); break;
    case kWebkitFontSmoothing: font.SetFontSmoothing(FontBuilder::InitialFontSmoothing()); break;
    case kWebkitLocale: font.SetLocale(FontBuilder::InitialLocale()); break;
    case kWritingMode: state.SetWritingMode(WritingMode::kHorizontalTb); break;
    case kHyphenateCharacter: builder.SetHyphenationString(AtomicString()); break;
    case kHyphenateLimitChars: builder.SetHyphenateLimitChars(StyleHyphenateLimitChars()); break;
    case kHyphens: builder.SetHyphens(Hyphens::kManual); break;
    case kLetterSpacing: builder.SetLetterSpacing(Length::Fixed()); break;
    case kLineHeight: builder.SetLineHeight(Length::Auto()); break;
    case kOverflowWrap: builder.SetOverflowWrap(EOverflowWrap::kNormal); break;
    case kTabSize: builder.SetTabSize(TabSize(8)); break;
    case kTextAlign: builder.SetTextAlign(ETextAlign::kStart); break;
    case kTextAlignLast: builder.SetTextAlignLast(ETextAlignLast::kAuto); break;
    case kTextAutospace: builder.SetTextAutospace(ETextAutospace::kNoAutospace); break;
    case kTextCombineUpright: builder.SetTextCombine(ETextCombine::kNone); break;
    case kTextDecorationColor: builder.SetTextDecorationColor(StyleColorValue::CurrentColor()); break;
    case kTextDecorationLine: builder.SetTextDecorationLine(TextDecorationLine::kNone); break;
    case kTextDecorationSkipInk: builder.SetTextDecorationSkipInk(ETextDecorationSkipInk::kAuto); break;
    case kTextDecorationStyle: builder.SetTextDecorationStyle(ETextDecorationStyle::kSolid); break;
    case kTextDecorationThickness: builder.SetTextDecorationThickness(TextDecorationThickness(Length::Auto())); break;
    case kTextEmphasisColor: builder.SetTextEmphasisColor(StyleColorValue::CurrentColor()); break;
    case kTextEmphasisPosition: builder.SetTextEmphasisPosition(TextEmphasisPosition::kOverRight); break;
    case kTextEmphasisStyle:
      builder.SetTextEmphasisFill(TextEmphasisFill::kFilled);
      builder.SetTextEmphasisMark(TextEmphasisMark::kNone);
      builder.SetTextEmphasisCustomMark(AtomicString());
      break;
    case kTextIndent: builder.SetTextIndent(Length::Fixed()); break;
    case kTextShadow: builder.SetTextShadow(nullptr); break;
    case kTextTransform: builder.SetTextTransform(ETextTransform::kNone); break;
    case kTextUnderlineOffset: builder.SetTextUnderlineOffset(Length()); break;
    case kTextUnderlinePosition: builder.SetTextUnderlinePosition(TextUnderlinePosition::kAuto); break;
    case kTextWrapMode: builder.SetTextWrapMode(TextWrapMode::kWrap); break;
    case kTextWrapStyle: builder.SetTextWrapStyle(TextWrapStyle::kAuto); break;
    case kUnicodeBidi: builder.SetUnicodeBidi(UnicodeBidi::kNormal); break;
    case kVerticalAlign: builder.SetVerticalAlign(EVerticalAlign::kBaseline); break;
    case kVisibility: builder.SetVisibility(EVisibility::kVisible); break;
    case kWebkitLineBreak: builder.SetLineBreak(LineBreak::kAuto); break;
    case kWebkitTextFillColor: builder.SetTextFillColor(StyleColorValue::CurrentColor()); break;
    case kWebkitTextStrokeColor: builder.SetTextStrokeColor(StyleColorValue::CurrentColor()); break;
    case kWebkitTextStrokeWidth: builder.SetTextStrokeWidth(0); break;
    case kWhiteSpaceCollapse: builder.SetWhiteSpaceCollapse(WhiteSpaceCollapse::kCollapse); break;
    case kWordBreak: builder.SetWordBreak(EWordBreak::kNormal); break;
    case kWordSpacing: builder.SetWordSpacing(Length::Fixed()); break;
    default: NOTREACHED();
  }
  (void)initial;
}

// Longhand::ApplyInherit().
void ApplyInherit(CSSPropertyID id, StyleResolverState& state) {
  ComputedStyleBuilder& builder = state.StyleBuilder();
  FontBuilder& font = state.GetFontBuilder();
  const ComputedStyle& parent = *state.ParentStyle();
  const FontDescription& parent_font = state.ParentFontDescription();
  switch (id) {
    case kColor: builder.SetColor(parent.Color()); break;
    case kDirection: builder.SetDirection(parent.Direction()); break;
    case kFontFamily: font.SetFamilyDescription(parent_font.GetFamilyDescription()); break;
    case kFontFeatureSettings: font.SetFeatureSettings(parent_font.SharedFeatureSettings()); break;
    case kFontKerning: font.SetKerning(parent_font.GetKerning()); break;
    case kFontOpticalSizing: font.SetFontOpticalSizing(parent_font.FontOpticalSizing()); break;
    case kFontPalette: font.SetFontPalette(parent_font.SharedFontPalette()); break;
    case kFontSize: font.SetSize(parent_font.GetSize()); break;
    case kFontSizeAdjust: font.SetSizeAdjust(parent_font.SizeAdjust()); break;
    case kFontStretch: font.SetStretch(parent_font.Stretch()); break;
    case kFontStyle: font.SetStyle(parent_font.Style()); break;
    case kFontSynthesisSmallCaps: font.SetFontSynthesisSmallCaps(parent_font.GetFontSynthesisSmallCaps()); break;
    case kFontSynthesisStyle: font.SetFontSynthesisStyle(parent_font.GetFontSynthesisStyle()); break;
    case kFontSynthesisWeight: font.SetFontSynthesisWeight(parent_font.GetFontSynthesisWeight()); break;
    case kFontVariantAlternates: font.SetFontVariantAlternates(parent_font.SharedFontVariantAlternates()); break;
    case kFontVariantCaps: font.SetVariantCaps(parent_font.VariantCaps()); break;
    case kFontVariantEastAsian: font.SetVariantEastAsian(parent_font.VariantEastAsian()); break;
    case kFontVariantEmoji: font.SetVariantEmoji(parent_font.VariantEmoji()); break;
    case kFontVariantLigatures: font.SetVariantLigatures(parent_font.GetVariantLigatures()); break;
    case kFontVariantNumeric: font.SetVariantNumeric(parent_font.VariantNumeric()); break;
    case kFontVariantPosition: font.SetVariantPosition(parent_font.VariantPosition()); break;
    case kFontVariationSettings: font.SetVariationSettings(parent_font.SharedVariationSettings()); break;
    case kFontWeight: font.SetWeight(parent_font.Weight()); break;
    case kTextOrientation: state.SetTextOrientation(parent.GetTextOrientation()); break;
    case kTextRendering: font.SetTextRendering(parent_font.TextRendering()); break;
    case kTextSpacingTrim: font.SetTextSpacingTrim(parent_font.GetTextSpacingTrim()); break;
    case kWebkitFontSmoothing: font.SetFontSmoothing(parent_font.FontSmoothing()); break;
    case kWebkitLocale: font.SetLocale(parent_font.Locale()); break;
    case kWritingMode: state.SetWritingMode(parent.GetWritingMode()); break;
    case kHyphenateCharacter: builder.SetHyphenationString(parent.HyphenationString()); break;
    case kHyphenateLimitChars: builder.SetHyphenateLimitChars(parent.HyphenateLimitChars()); break;
    case kHyphens: builder.SetHyphens(parent.GetHyphens()); break;
    case kLetterSpacing: builder.SetLetterSpacing(parent.ComputedLetterSpacing()); break;
    case kLineHeight: builder.SetLineHeight(parent.SpecifiedLineHeight()); break;
    case kOverflowWrap: builder.SetOverflowWrap(parent.OverflowWrap()); break;
    case kTabSize: builder.SetTabSize(parent.GetTabSize()); break;
    case kTextAlign: builder.SetTextAlign(parent.GetTextAlign()); break;
    case kTextAlignLast: builder.SetTextAlignLast(parent.TextAlignLast()); break;
    case kTextAutospace: builder.SetTextAutospace(parent.TextAutospace()); break;
    case kTextCombineUpright: builder.SetTextCombine(parent.TextCombine()); break;
    case kTextDecorationColor: builder.SetTextDecorationColor(parent.TextDecorationColor()); break;
    case kTextDecorationLine: builder.SetTextDecorationLine(parent.GetTextDecorationLine()); break;
    case kTextDecorationSkipInk: builder.SetTextDecorationSkipInk(parent.TextDecorationSkipInk()); break;
    case kTextDecorationStyle: builder.SetTextDecorationStyle(parent.TextDecorationStyle()); break;
    case kTextDecorationThickness: builder.SetTextDecorationThickness(parent.GetTextDecorationThickness()); break;
    case kTextEmphasisColor: builder.SetTextEmphasisColor(parent.TextEmphasisColor()); break;
    case kTextEmphasisPosition: builder.SetTextEmphasisPosition(parent.GetTextEmphasisPosition()); break;
    case kTextEmphasisStyle:
      builder.SetTextEmphasisFill(parent.GetTextEmphasisFill());
      builder.SetTextEmphasisMark(parent.GetTextEmphasisMark());
      builder.SetTextEmphasisCustomMark(parent.TextEmphasisCustomMark());
      break;
    case kTextIndent: builder.SetTextIndent(parent.TextIndent()); break;
    case kTextShadow: builder.SetTextShadow(parent.SharedTextShadow()); break;
    case kTextTransform: builder.SetTextTransform(parent.TextTransform()); break;
    case kTextUnderlineOffset: builder.SetTextUnderlineOffset(parent.TextUnderlineOffset()); break;
    case kTextUnderlinePosition: builder.SetTextUnderlinePosition(parent.GetTextUnderlinePosition()); break;
    case kTextWrapMode: builder.SetTextWrapMode(parent.GetTextWrapMode()); break;
    case kTextWrapStyle: builder.SetTextWrapStyle(parent.GetTextWrapStyle()); break;
    case kUnicodeBidi: builder.SetUnicodeBidi(parent.GetUnicodeBidi()); break;
    case kVerticalAlign: {
      EVerticalAlign vertical_align = parent.VerticalAlign();
      builder.SetVerticalAlign(vertical_align);
      if (vertical_align == EVerticalAlign::kLength) builder.SetVerticalAlignLength(parent.GetVerticalAlignLength());
      break;
    }
    case kVisibility: builder.SetVisibility(parent.Visibility()); break;
    case kWebkitLineBreak: builder.SetLineBreak(parent.GetLineBreak()); break;
    case kWebkitTextFillColor: builder.SetTextFillColor(parent.TextFillColor()); break;
    case kWebkitTextStrokeColor: builder.SetTextStrokeColor(parent.TextStrokeColor()); break;
    case kWebkitTextStrokeWidth: builder.SetTextStrokeWidth(parent.TextStrokeWidth()); break;
    case kWhiteSpaceCollapse: builder.SetWhiteSpaceCollapse(parent.GetWhiteSpaceCollapse()); break;
    case kWordBreak: builder.SetWordBreak(parent.WordBreak()); break;
    case kWordSpacing: builder.SetWordSpacing(parent.ComputedWordSpacing()); break;
    default: NOTREACHED();
  }
}

// Longhand::ApplyValue().
void ApplyValue(CSSPropertyID id, StyleResolverState& state, const CSSValue& value) {
  ComputedStyleBuilder& builder = state.StyleBuilder();
  FontBuilder& font = state.GetFontBuilder();
  switch (id) {
    case kColor:
      if (value.IsIdentifierValue()) {
        // As per the spec, 'color: currentColor' is treated as 'color:
        // inherit'. The root has no parent and takes the initial color.
        if (state.ParentStyle()) ApplyInherit(id, state);
        else ApplyInitial(id, state);
      } else {
        builder.SetColor(ToColor4f(To<cssvalue::CSSColor>(value).Value()));
      }
      break;
    case kDirection: builder.SetDirection(Ident(value).ConvertTo<TextDirection>()); break;
    case kFontFamily: font.SetFamilyDescription(Converter::ConvertFontFamily(state, value)); break;
    case kFontFeatureSettings: font.SetFeatureSettings(Converter::ConvertFontFeatureSettings(state, value)); break;
    case kFontKerning: font.SetKerning(Converter::ConvertFontKerning(state, value)); break;
    case kFontOpticalSizing: font.SetFontOpticalSizing(Converter::ConvertFontOpticalSizing(state, value)); break;
    case kFontPalette: font.SetFontPalette(Converter::ConvertFontPalette(state, value)); break;
    case kFontSize: font.SetSize(Converter::ConvertFontSize(state, value)); break;
    case kFontSizeAdjust: font.SetSizeAdjust(Converter::ConvertFontSizeAdjust(state, value)); break;
    case kFontStretch: font.SetStretch(Converter::ConvertFontStretch(state, value)); break;
    case kFontStyle: font.SetStyle(Converter::ConvertFontStyle(state, value)); break;
    case kFontSynthesisSmallCaps:
      font.SetFontSynthesisSmallCaps(Ident(value).ConvertTo<FontDescription::FontSynthesisSmallCaps>());
      break;
    case kFontSynthesisStyle:
      font.SetFontSynthesisStyle(Ident(value).ConvertTo<FontDescription::FontSynthesisStyle>());
      break;
    case kFontSynthesisWeight:
      font.SetFontSynthesisWeight(Ident(value).ConvertTo<FontDescription::FontSynthesisWeight>());
      break;
    case kFontVariantAlternates:
      font.SetFontVariantAlternates(Converter::ConvertFontVariantAlternates(state, value));
      break;
    case kFontVariantCaps: font.SetVariantCaps(Converter::ConvertFontVariantCaps(state, value)); break;
    case kFontVariantEastAsian: font.SetVariantEastAsian(Converter::ConvertFontVariantEastAsian(state, value)); break;
    case kFontVariantEmoji: font.SetVariantEmoji(Converter::ConvertFontVariantEmoji(state, value)); break;
    case kFontVariantLigatures: font.SetVariantLigatures(Converter::ConvertFontVariantLigatures(state, value)); break;
    case kFontVariantNumeric: font.SetVariantNumeric(Converter::ConvertFontVariantNumeric(state, value)); break;
    case kFontVariantPosition: font.SetVariantPosition(Converter::ConvertFontVariantPosition(state, value)); break;
    case kFontVariationSettings: font.SetVariationSettings(Converter::ConvertFontVariationSettings(state, value)); break;
    case kFontWeight: font.SetWeight(Converter::ConvertFontWeight(state, value)); break;
    case kTextOrientation: state.SetTextOrientation(Ident(value).ConvertTo<ETextOrientation>()); break;
    case kTextRendering: font.SetTextRendering(Ident(value).ConvertTo<TextRenderingMode>()); break;
    case kTextSpacingTrim: font.SetTextSpacingTrim(Ident(value).ConvertTo<TextSpacingTrim>()); break;
    case kWebkitFontSmoothing: font.SetFontSmoothing(Ident(value).ConvertTo<FontSmoothingMode>()); break;
    case kWebkitLocale:
      if (value.IsIdentifierValue()) font.SetLocale(nullptr);
      else font.SetLocale(LayoutLocale::Get(AtomicString(To<CSSStringValue>(value).Value())));
      break;
    case kWritingMode: state.SetWritingMode(Ident(value).ConvertTo<WritingMode>()); break;
    case kHyphenateCharacter: builder.SetHyphenationString(Converter::ConvertStringOrAuto(state, value)); break;
    case kHyphenateLimitChars: builder.SetHyphenateLimitChars(Converter::ConvertHyphenateLimitChars(state, value)); break;
    case kHyphens: builder.SetHyphens(Ident(value).ConvertTo<Hyphens>()); break;
    case kLetterSpacing: builder.SetLetterSpacing(Converter::ConvertSpacing(state, value)); break;
    case kLineHeight: builder.SetLineHeight(Converter::ConvertLineHeight(state, value)); break;
    case kOverflowWrap: builder.SetOverflowWrap(Ident(value).ConvertTo<EOverflowWrap>()); break;
    case kTabSize: builder.SetTabSize(Converter::ConvertLengthOrTabSpaces(state, value)); break;
    case kTextAlign: {
      // TextAlign::ApplyValue(). There are no th elements, so
      // -internal-center is plain center.
      const ComputedStyle& parent = state.ParentStyle() ? *state.ParentStyle() : state.InitialStyle();
      const auto* ident_value = DynamicTo<CSSIdentifierValue>(value);
      if (ident_value && ident_value->GetValueID() != CSSValueID::kWebkitMatchParent) {
        if (ident_value->GetValueID() == CSSValueID::kInternalCenter &&
            parent.GetTextAlign() != ETextAlign::kStart) {
          builder.SetTextAlign(parent.GetTextAlign());
        } else {
          builder.SetTextAlign(ident_value->ConvertTo<ETextAlign>());
        }
      } else if (parent.GetTextAlign() == ETextAlign::kStart) {
        builder.SetTextAlign(parent.IsLeftToRightDirection() ? ETextAlign::kLeft : ETextAlign::kRight);
      } else if (parent.GetTextAlign() == ETextAlign::kEnd) {
        builder.SetTextAlign(parent.IsLeftToRightDirection() ? ETextAlign::kRight : ETextAlign::kLeft);
      } else {
        builder.SetTextAlign(parent.GetTextAlign());
      }
      break;
    }
    case kTextAlignLast: builder.SetTextAlignLast(Ident(value).ConvertTo<ETextAlignLast>()); break;
    case kTextAutospace: builder.SetTextAutospace(Ident(value).ConvertTo<ETextAutospace>()); break;
    case kTextCombineUpright: builder.SetTextCombine(Ident(value).ConvertTo<ETextCombine>()); break;
    case kTextDecorationColor: builder.SetTextDecorationColor(Converter::ConvertStyleColor(state, value)); break;
    case kTextDecorationLine:
      builder.SetTextDecorationLine(Converter::ConvertFlags<TextDecorationLine>(state, value));
      break;
    case kTextDecorationSkipInk: builder.SetTextDecorationSkipInk(Ident(value).ConvertTo<ETextDecorationSkipInk>()); break;
    case kTextDecorationStyle: builder.SetTextDecorationStyle(Ident(value).ConvertTo<ETextDecorationStyle>()); break;
    case kTextDecorationThickness:
      builder.SetTextDecorationThickness(Converter::ConvertTextDecorationThickness(state, value));
      break;
    case kTextEmphasisColor: builder.SetTextEmphasisColor(Converter::ConvertStyleColor(state, value)); break;
    case kTextEmphasisPosition:
      builder.SetTextEmphasisPosition(Converter::ConvertTextTextEmphasisPosition(state, value));
      break;
    case kTextEmphasisStyle: {
      // TextEmphasisStyle::ApplyValue().
      const CSSValue* style = &value;
      if (const auto* list = DynamicTo<CSSValueList>(style)) {
        if (list->length() == 1) style = &list->First();
      }
      if (const auto* list = DynamicTo<CSSValueList>(style)) {
        assert(list->length() == 2U);
        for (unsigned i = 0; i < 2; ++i) {
          const auto& ident_value = To<CSSIdentifierValue>(list->Item(i));
          if (ident_value.GetValueID() == CSSValueID::kFilled || ident_value.GetValueID() == CSSValueID::kOpen)
            builder.SetTextEmphasisFill(ident_value.ConvertTo<TextEmphasisFill>());
          else
            builder.SetTextEmphasisMark(ident_value.ConvertTo<TextEmphasisMark>());
        }
        builder.SetTextEmphasisCustomMark(g_null_atom);
        break;
      }
      if (auto* string_value = DynamicTo<CSSStringValue>(style)) {
        builder.SetTextEmphasisFill(TextEmphasisFill::kFilled);
        builder.SetTextEmphasisMark(TextEmphasisMark::kCustom);
        builder.SetTextEmphasisCustomMark(AtomicString(string_value->Value()));
        break;
      }
      const CSSIdentifierValue& identifier_value = To<CSSIdentifierValue>(*style);
      builder.SetTextEmphasisCustomMark(g_null_atom);
      if (identifier_value.GetValueID() == CSSValueID::kFilled || identifier_value.GetValueID() == CSSValueID::kOpen) {
        builder.SetTextEmphasisFill(identifier_value.ConvertTo<TextEmphasisFill>());
        builder.SetTextEmphasisMark(TextEmphasisMark::kAuto);
      } else {
        builder.SetTextEmphasisFill(TextEmphasisFill::kFilled);
        builder.SetTextEmphasisMark(identifier_value.ConvertTo<TextEmphasisMark>());
      }
      break;
    }
    case kTextIndent: {
      // TextIndent::ApplyValue().
      Length length_or_percentage_value;
      for (const auto& list_value : To<CSSValueList>(value)) {
        if (auto* list_primitive_value = DynamicTo<CSSPrimitiveValue>(*list_value))
          length_or_percentage_value = list_primitive_value->ConvertToLength(state.CssToLengthConversionData());
        else
          NOTREACHED();
      }
      builder.SetTextIndent(length_or_percentage_value);
      break;
    }
    case kTextShadow: builder.SetTextShadow(Converter::ConvertShadowList(state, value)); break;
    case kTextTransform: builder.SetTextTransform(Ident(value).ConvertTo<ETextTransform>()); break;
    case kTextUnderlineOffset: builder.SetTextUnderlineOffset(Converter::ConvertTextUnderlineOffset(state, value)); break;
    case kTextUnderlinePosition:
      builder.SetTextUnderlinePosition(Converter::ConvertTextUnderlinePosition(state, value));
      break;
    case kTextWrapMode: builder.SetTextWrapMode(Ident(value).ConvertTo<TextWrapMode>()); break;
    case kTextWrapStyle: builder.SetTextWrapStyle(Ident(value).ConvertTo<TextWrapStyle>()); break;
    case kUnicodeBidi: builder.SetUnicodeBidi(Ident(value).ConvertTo<UnicodeBidi>()); break;
    case kVerticalAlign:
      if (auto* identifier_value = DynamicTo<CSSIdentifierValue>(value))
        builder.SetVerticalAlign(identifier_value->ConvertTo<EVerticalAlign>());
      else
        builder.SetVerticalAlignLength(To<CSSPrimitiveValue>(value).ConvertToLength(state.CssToLengthConversionData()));
      break;
    case kVisibility: builder.SetVisibility(Ident(value).ConvertTo<EVisibility>()); break;
    case kWebkitLineBreak: builder.SetLineBreak(Ident(value).ConvertTo<LineBreak>()); break;
    case kWebkitTextFillColor: builder.SetTextFillColor(Converter::ConvertStyleColor(state, value)); break;
    case kWebkitTextStrokeColor: builder.SetTextStrokeColor(Converter::ConvertStyleColor(state, value)); break;
    case kWebkitTextStrokeWidth: builder.SetTextStrokeWidth(Converter::ConvertTextStrokeWidth(state, value)); break;
    case kWhiteSpaceCollapse: builder.SetWhiteSpaceCollapse(Ident(value).ConvertTo<WhiteSpaceCollapse>()); break;
    case kWordBreak: builder.SetWordBreak(Ident(value).ConvertTo<EWordBreak>()); break;
    case kWordSpacing: builder.SetWordSpacing(Converter::ConvertSpacing(state, value)); break;
    default: NOTREACHED();
  }
}

} // namespace

void StyleBuilder::ApplyPhysicalProperty(CSSPropertyID id, StyleResolverState& state, const CSSValue& value) {
  assert(IsLonghand(id) && GetCSSPropertyMetadata(id)->surrogate_for == CSSPropertyID::kInvalid);
  bool is_inherit = value.IsInheritedValue();
  bool is_initial = value.IsInitialValue();
  bool is_unset = value.IsUnsetValue();
  if ((is_inherit || is_unset) && !state.ParentStyle()) {
    is_inherit = false;
    is_unset = false;
    is_initial = true;
  }
  assert(!is_inherit || !is_initial);

  const bool is_inherited_for_unset = state.IsInheritedForUnset(id);
  if (is_inherit && !is_inherited_for_unset) {
    // ChildHasExplicitInheritance on the parent is not ported; the host
    // recalculates children of a parent whose inherited data is unchanged
    // when they have explicit inheritance.
    state.StyleBuilder().SetHasExplicitInheritance();
  } else if (is_unset) {
    assert(!is_inherit && !is_initial);
    if (is_inherited_for_unset) is_inherit = true;
    else is_initial = true;
  }

  if (is_initial) ApplyInitial(id, state);
  else if (is_inherit) ApplyInherit(id, state);
  else ApplyValue(id, state, value);
}

} // namespace bkit
