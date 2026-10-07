// Typed construction of the CSSValues the CSS parser produces.
//
// Each property struct stands for the upstream css_longhand:: or
// css_shorthand:: class of the same name (core/css/properties/longhands.h,
// shorthands.h). Its Make() is the value-building half of that class's
// ParseSingleValue() or ParseShorthand(): the reading of tokens is replaced
// by the component types below, which keep the distinctions only tokens
// carry (a <number> or an <integer>, a percentage or a length, ...). Checks
// that do not depend on the token form (ranges, keyword sets, angle limits)
// and the construction of the canonical value both happen in Make(), so a
// CSS parser can later read tokens into components and call the same Make().
//
// Make() has no side effects. It returns null (false for a shorthand, which
// then appends nothing) for invalid input, so a parser can go on to the next
// alternative as the Consume*() functions do. CSS-wide keywords are not
// components; StyleDeclaration sets them. The parsing context is a style
// rule's: @font-face descriptors, quirks and UA-only keywords are not ported.
#pragma once

#include <memory>
#include <optional>
#include <variant>

#include "base/memory/scoped_refptr.h"
#include "base/text/atomic_string.h"
#include "base/text/wtf_string.h"
#include "base/vector.h"
#include "paint/color.h"
#include "style/css_math_function_value.h"
#include "style/css_primitive_value.h"
#include "style/css_property_names.h"
#include "style/css_value.h"
#include "style/css_value_keywords.h"
#include "style/style_color.h"

namespace bkfont {

// A <number> token (UnitType::kNumber).
struct CSSNumber {
  double value;
};

// A <number> token of the integer type, as ConsumeInteger() takes it.
struct CSSInteger {
  int value;
};

// A dimension or percentage token: a length, an angle or a percentage, by
// its unit.
struct CSSLength {
  double value;
  CSSPrimitiveValue::UnitType unit;
};

// A math function of the local subset: a linear sum of terms.
struct CSSCalc {
  Vector<CSSMathFunctionValue::Term> terms;
};

// A <length> or a math function, where the grammar takes only those.
using CSSLengthOrCalc = std::variant<CSSLength, CSSCalc>;

// One <family-name> or <generic-family> of font-family. A generic family is
// a keyword (serif ... math); otherwise `name` is the family name, as a
// string token or a sequence of identifiers gives it.
struct CSSFontFamilyName {
  CSSValueID generic = CSSValueID::kInvalid;
  AtomicString name;
};

// font-style: oblique <angle>. A bare 'oblique' is the keyword.
struct CSSFontStyleOblique {
  CSSLengthOrCalc angle;
};

// CSSPropertyValue: a longhand and its shared, immutable value, as
// ParseShorthand() appends them.
struct CSSPropertyValue {
  CSSPropertyID property;
  scoped_refptr<const CSSValue> value;
  bool operator==(const CSSPropertyValue&) const;
};
using CSSPropertyValues = Vector<CSSPropertyValue>;

namespace css_longhand {

// A keyword property: CSSParserFastPaths::IsValidKeywordPropertyAndValue(),
// or a ParseSingleValue() that only takes some keywords.
template <CSSPropertyID property>
struct KeywordLonghand {
  static constexpr CSSPropertyID kId = property;
  using Input = CSSValueID;
};

// ConsumeColor() for the local color subset: currentcolor or an RGBA color.
template <CSSPropertyID property>
struct ColorLonghand {
  static constexpr CSSPropertyID kId = property;
  using Input = StyleColorValue;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

using Color = ColorLonghand<CSSPropertyID::kColor>;

struct Direction : KeywordLonghand<CSSPropertyID::kDirection> {
  static scoped_refptr<const CSSValue> Make(Input);
};

// ConsumeFontFamily().
struct FontFamily {
  static constexpr CSSPropertyID kId = CSSPropertyID::kFontFamily;
  using Input = Vector<CSSFontFamilyName>;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

// ConsumeFontFeatureSettings(): normal, or a list of features.
struct FontFeatureSettings {
  static constexpr CSSPropertyID kId = CSSPropertyID::kFontFeatureSettings;
  // ConsumeFontSettingsTagAndValue(): a four-character tag and an <integer>
  // (a CSSInteger or a math function), 'on' / 'off', or nothing (monostate),
  // which is 1.
  struct Feature {
    AtomicString tag;
    std::variant<std::monostate, CSSValueID, CSSInteger, CSSCalc> value;
  };
  using Input = std::variant<CSSValueID, Vector<Feature>>;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

struct FontKerning : KeywordLonghand<CSSPropertyID::kFontKerning> {
  static scoped_refptr<const CSSValue> Make(Input);
};

struct FontOpticalSizing : KeywordLonghand<CSSPropertyID::kFontOpticalSizing> {
  static scoped_refptr<const CSSValue> Make(Input);
};

// ConsumeFontPalette(): normal | light | dark, a <dashed-ident> or a
// palette-mix().
struct FontPalette {
  static constexpr CSSPropertyID kId = CSSPropertyID::kFontPalette;
  struct Mix;
  using Input = std::variant<CSSValueID, AtomicString, std::unique_ptr<Mix>>;
  // ConsumePaletteMixFunction().
  struct Mix {
    // <color-interpolation-method> (ConsumeColorInterpolationSpace()); a hue
    // interpolation method other than kShorter needs a polar space. kNone is
    // no method.
    bkfont::Color::ColorSpace color_space = bkfont::Color::ColorSpace::kNone;
    bkfont::Color::HueInterpolationMethod hue_interpolation = bkfont::Color::HueInterpolationMethod::kShorter;
    Input palette1;
    // A percentage in [0, 100] (a CSSLength) or a math function.
    std::optional<CSSLengthOrCalc> percentage1;
    Input palette2;
    std::optional<CSSLengthOrCalc> percentage2;
  };
  static scoped_refptr<const CSSValue> Make(const Input&);
};

// ConsumeFontSize().
struct FontSize {
  static constexpr CSSPropertyID kId = CSSPropertyID::kFontSize;
  using Input = std::variant<CSSValueID, CSSLength, CSSCalc>;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

// ConsumeFontSizeAdjust(): none | from-font, a number, or a font metric with
// one of those.
struct FontSizeAdjust {
  static constexpr CSSPropertyID kId = CSSPropertyID::kFontSizeAdjust;
  // An ex-height metric is implied and dropped.
  struct WithMetric {
    CSSValueID metric;
    std::variant<CSSValueID, CSSNumber, CSSCalc> value;
  };
  using Input = std::variant<CSSValueID, CSSNumber, CSSCalc, WithMetric>;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

// ConsumeFontStretch(), outside @font-face.
struct FontStretch {
  static constexpr CSSPropertyID kId = CSSPropertyID::kFontStretch;
  using Input = std::variant<CSSValueID, CSSLength, CSSCalc>;
  static scoped_refptr<const CSSValue> Make(const Input&);
  // ConsumeFontStretchKeywordOnly(), as the font shorthand takes it.
  static scoped_refptr<const CSSValue> MakeKeywordOnly(CSSValueID);
};

// ConsumeFontStyle(), outside @font-face.
struct FontStyle {
  static constexpr CSSPropertyID kId = CSSPropertyID::kFontStyle;
  using Input = std::variant<CSSValueID, CSSFontStyleOblique>;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

struct FontSynthesisSmallCaps : KeywordLonghand<CSSPropertyID::kFontSynthesisSmallCaps> {
  static scoped_refptr<const CSSValue> Make(Input);
};

struct FontSynthesisStyle : KeywordLonghand<CSSPropertyID::kFontSynthesisStyle> {
  static scoped_refptr<const CSSValue> Make(Input);
};

struct FontSynthesisWeight : KeywordLonghand<CSSPropertyID::kFontSynthesisWeight> {
  static scoped_refptr<const CSSValue> Make(Input);
};

// FontVariantAlternates::ParseSingleValue(): normal, or the alternates
// (FontVariantAlternatesParser).
struct FontVariantAlternates {
  static constexpr CSSPropertyID kId = CSSPropertyID::kFontVariantAlternates;
  // At least one of them. The aliases are <custom-ident>s; styleset() and
  // character-variant() take one or more, the others exactly one.
  struct Alternates {
    bool historical_forms = false;
    std::optional<AtomicString> stylistic;
    Vector<AtomicString> styleset;
    Vector<AtomicString> character_variant;
    std::optional<AtomicString> swash;
    std::optional<AtomicString> ornaments;
    std::optional<AtomicString> annotation;
  };
  using Input = std::variant<CSSValueID, Alternates>;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

struct FontVariantCaps : KeywordLonghand<CSSPropertyID::kFontVariantCaps> {
  static scoped_refptr<const CSSValue> Make(Input);
};

// FontVariantEastAsian::ParseSingleValue(): normal, or keywords of distinct
// groups (FontVariantEastAsianParser), stored form, width, ruby.
struct FontVariantEastAsian {
  static constexpr CSSPropertyID kId = CSSPropertyID::kFontVariantEastAsian;
  using Input = std::variant<CSSValueID, Vector<CSSValueID>>;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

struct FontVariantEmoji : KeywordLonghand<CSSPropertyID::kFontVariantEmoji> {
  static scoped_refptr<const CSSValue> Make(Input);
};

// FontVariantLigatures::ParseSingleValue(): normal | none, or keywords of
// distinct groups in their order (FontVariantLigaturesParser).
struct FontVariantLigatures {
  static constexpr CSSPropertyID kId = CSSPropertyID::kFontVariantLigatures;
  using Input = std::variant<CSSValueID, Vector<CSSValueID>>;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

// FontVariantNumeric::ParseSingleValue(): normal, or keywords of distinct
// groups in their order (FontVariantNumericParser).
struct FontVariantNumeric {
  static constexpr CSSPropertyID kId = CSSPropertyID::kFontVariantNumeric;
  using Input = std::variant<CSSValueID, Vector<CSSValueID>>;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

struct FontVariantPosition : KeywordLonghand<CSSPropertyID::kFontVariantPosition> {
  static scoped_refptr<const CSSValue> Make(Input);
};

// FontVariationSettings::ParseSingleValue(): normal, or a list of axes.
struct FontVariationSettings {
  static constexpr CSSPropertyID kId = CSSPropertyID::kFontVariationSettings;
  // ConsumeFontVariationTag(): a four-character tag and a <number>.
  struct Axis {
    AtomicString tag;
    std::variant<CSSNumber, CSSCalc> value;
  };
  using Input = std::variant<CSSValueID, Vector<Axis>>;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

// ConsumeFontWeight(), outside @font-face.
struct FontWeight {
  static constexpr CSSPropertyID kId = CSSPropertyID::kFontWeight;
  using Input = std::variant<CSSValueID, CSSNumber, CSSCalc>;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

// HyphenateCharacter::ParseSingleValue(): auto or a string.
struct HyphenateCharacter {
  static constexpr CSSPropertyID kId = CSSPropertyID::kHyphenateCharacter;
  using Input = std::variant<CSSValueID, String>;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

// ConsumeHyphenateLimitChars(): one to three of auto or an <integer [1,∞]>.
struct HyphenateLimitChars {
  static constexpr CSSPropertyID kId = CSSPropertyID::kHyphenateLimitChars;
  using Input = Vector<std::variant<CSSValueID, CSSInteger, CSSCalc>>;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

// USE_MINIKIN_HYPHENATION: all platforms hyphenate like Linux.
struct Hyphens : KeywordLonghand<CSSPropertyID::kHyphens> {
  static scoped_refptr<const CSSValue> Make(Input);
};

// ParseSpacing(), without CSSLetterAndWordSpacingPercentage.
struct LetterSpacing {
  static constexpr CSSPropertyID kId = CSSPropertyID::kLetterSpacing;
  using Input = std::variant<CSSValueID, CSSLength, CSSCalc>;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

struct LineBreak : KeywordLonghand<CSSPropertyID::kLineBreak> {
  static scoped_refptr<const CSSValue> Make(Input);
};

// ConsumeLineHeight().
struct LineHeight {
  static constexpr CSSPropertyID kId = CSSPropertyID::kLineHeight;
  using Input = std::variant<CSSValueID, CSSNumber, CSSLength, CSSCalc>;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

struct OverflowWrap : KeywordLonghand<CSSPropertyID::kOverflowWrap> {
  static scoped_refptr<const CSSValue> Make(Input);
};

// TabSize::ParseSingleValue(): a <number [0,∞]> or a <length [0,∞]>.
struct TabSize {
  static constexpr CSSPropertyID kId = CSSPropertyID::kTabSize;
  using Input = std::variant<CSSNumber, CSSLength, CSSCalc>;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

struct TextAlign : KeywordLonghand<CSSPropertyID::kTextAlign> {
  static scoped_refptr<const CSSValue> Make(Input);
};

struct TextAlignLast : KeywordLonghand<CSSPropertyID::kTextAlignLast> {
  static scoped_refptr<const CSSValue> Make(Input);
};

struct TextAutospace : KeywordLonghand<CSSPropertyID::kTextAutospace> {
  static scoped_refptr<const CSSValue> Make(Input);
};

struct TextCombineUpright : KeywordLonghand<CSSPropertyID::kTextCombineUpright> {
  static scoped_refptr<const CSSValue> Make(Input);
};

using TextDecorationColor = ColorLonghand<CSSPropertyID::kTextDecorationColor>;

// ConsumeTextDecorationLine(): none | spelling-error | grammar-error, or
// distinct lines of underline, overline, line-through and blink, stored in
// that order.
struct TextDecorationLine {
  static constexpr CSSPropertyID kId = CSSPropertyID::kTextDecorationLine;
  using Input = std::variant<CSSValueID, Vector<CSSValueID>>;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

struct TextDecorationSkipInk : KeywordLonghand<CSSPropertyID::kTextDecorationSkipInk> {
  static scoped_refptr<const CSSValue> Make(Input);
};

struct TextDecorationStyle : KeywordLonghand<CSSPropertyID::kTextDecorationStyle> {
  static scoped_refptr<const CSSValue> Make(Input);
};

// TextDecorationThickness::ParseSingleValue(): from-font | auto or a
// <length-percentage>.
struct TextDecorationThickness {
  static constexpr CSSPropertyID kId = CSSPropertyID::kTextDecorationThickness;
  using Input = std::variant<CSSValueID, CSSLength, CSSCalc>;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

using TextEmphasisColor = ColorLonghand<CSSPropertyID::kTextEmphasisColor>;

// TextEmphasisPosition::ParseSingleValue(), without the experimental
// TextEmphasisPositionAuto: over | under, then optionally left | right.
struct TextEmphasisPosition {
  static constexpr CSSPropertyID kId = CSSPropertyID::kTextEmphasisPosition;
  struct Input {
    CSSValueID over_under;
    // kInvalid when omitted.
    CSSValueID left_right = CSSValueID::kInvalid;
  };
  static scoped_refptr<const CSSValue> Make(const Input&);
};

// TextEmphasisStyle::ParseSingleValue(): none, a string, a fill or a shape
// keyword, or both.
struct TextEmphasisStyle {
  static constexpr CSSPropertyID kId = CSSPropertyID::kTextEmphasisStyle;
  struct FillAndShape {
    CSSValueID fill;
    CSSValueID shape;
  };
  using Input = std::variant<CSSValueID, String, FillAndShape>;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

// TextIndent::ParseSingleValue(): a <length-percentage>, stored in a list.
struct TextIndent {
  static constexpr CSSPropertyID kId = CSSPropertyID::kTextIndent;
  using Input = std::variant<CSSLength, CSSCalc>;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

struct TextOrientation : KeywordLonghand<CSSPropertyID::kTextOrientation> {
  static scoped_refptr<const CSSValue> Make(Input);
};

struct TextRendering : KeywordLonghand<CSSPropertyID::kTextRendering> {
  static scoped_refptr<const CSSValue> Make(Input);
};

// ConsumeShadow() with AllowInsetAndSpread::kForbid: none, or a list.
struct TextShadow {
  static constexpr CSSPropertyID kId = CSSPropertyID::kTextShadow;
  // ParseSingleShadow(): the offsets, the blur radius and the color.
  struct Shadow {
    CSSLengthOrCalc x;
    CSSLengthOrCalc y;
    std::optional<CSSLengthOrCalc> blur;
    std::optional<StyleColorValue> color;
  };
  using Input = std::variant<CSSValueID, Vector<Shadow>>;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

struct TextSpacingTrim : KeywordLonghand<CSSPropertyID::kTextSpacingTrim> {
  static scoped_refptr<const CSSValue> Make(Input);
};

struct TextTransform : KeywordLonghand<CSSPropertyID::kTextTransform> {
  static scoped_refptr<const CSSValue> Make(Input);
};

// TextUnderlineOffset::ParseSingleValue(): auto or a <length-percentage>.
struct TextUnderlineOffset {
  static constexpr CSSPropertyID kId = CSSPropertyID::kTextUnderlineOffset;
  using Input = std::variant<CSSValueID, CSSLength, CSSCalc>;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

// TextUnderlinePosition::ParseSingleValue(): auto, or from-font | under
// and / or left | right, stored in that order.
struct TextUnderlinePosition {
  static constexpr CSSPropertyID kId = CSSPropertyID::kTextUnderlinePosition;
  // At least one; kInvalid when omitted.
  struct Parts {
    CSSValueID position = CSSValueID::kInvalid;
    CSSValueID side = CSSValueID::kInvalid;
  };
  using Input = std::variant<CSSValueID, Parts>;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

struct TextWrapMode : KeywordLonghand<CSSPropertyID::kTextWrapMode> {
  static scoped_refptr<const CSSValue> Make(Input);
};

struct TextWrapStyle : KeywordLonghand<CSSPropertyID::kTextWrapStyle> {
  static scoped_refptr<const CSSValue> Make(Input);
};

struct UnicodeBidi : KeywordLonghand<CSSPropertyID::kUnicodeBidi> {
  static scoped_refptr<const CSSValue> Make(Input);
};

// VerticalAlign::ParseSingleValue(): a keyword or a <length-percentage>.
struct VerticalAlign {
  static constexpr CSSPropertyID kId = CSSPropertyID::kVerticalAlign;
  using Input = std::variant<CSSValueID, CSSLength, CSSCalc>;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

struct Visibility : KeywordLonghand<CSSPropertyID::kVisibility> {
  static scoped_refptr<const CSSValue> Make(Input);
};

struct WebkitFontSmoothing : KeywordLonghand<CSSPropertyID::kWebkitFontSmoothing> {
  static scoped_refptr<const CSSValue> Make(Input);
};

struct WebkitLineBreak : KeywordLonghand<CSSPropertyID::kWebkitLineBreak> {
  static scoped_refptr<const CSSValue> Make(Input);
};

// WebkitLocale::ParseSingleValue(): auto or a string.
struct WebkitLocale {
  static constexpr CSSPropertyID kId = CSSPropertyID::kWebkitLocale;
  using Input = std::variant<CSSValueID, String>;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

using WebkitTextFillColor = ColorLonghand<CSSPropertyID::kWebkitTextFillColor>;

struct WebkitTextOrientation : KeywordLonghand<CSSPropertyID::kWebkitTextOrientation> {
  static scoped_refptr<const CSSValue> Make(Input);
};

using WebkitTextStrokeColor = ColorLonghand<CSSPropertyID::kWebkitTextStrokeColor>;

// ConsumeLineWidth(): thin | medium | thick or a <length [0,∞]>.
struct WebkitTextStrokeWidth {
  static constexpr CSSPropertyID kId = CSSPropertyID::kWebkitTextStrokeWidth;
  using Input = std::variant<CSSValueID, CSSLength, CSSCalc>;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

struct WebkitWritingMode : KeywordLonghand<CSSPropertyID::kWebkitWritingMode> {
  static scoped_refptr<const CSSValue> Make(Input);
};

struct WhiteSpaceCollapse : KeywordLonghand<CSSPropertyID::kWhiteSpaceCollapse> {
  static scoped_refptr<const CSSValue> Make(Input);
};

struct WordBreak : KeywordLonghand<CSSPropertyID::kWordBreak> {
  static scoped_refptr<const CSSValue> Make(Input);
};

// ParseSpacing(), without CSSLetterAndWordSpacingPercentage.
struct WordSpacing {
  static constexpr CSSPropertyID kId = CSSPropertyID::kWordSpacing;
  using Input = std::variant<CSSValueID, CSSLength, CSSCalc>;
  static scoped_refptr<const CSSValue> Make(const Input&);
};

struct WritingMode : KeywordLonghand<CSSPropertyID::kWritingMode> {
  static scoped_refptr<const CSSValue> Make(Input);
};

} // namespace css_longhand

namespace css_shorthand {

// Font::ParseShorthand() -> ConsumeFont(). System fonts are not ported.
// Omitted optional components and the reset-only longhands get the explicit
// keywords ConsumeFont() gives them; the longhands are appended in its order.
struct Font {
  static constexpr CSSPropertyID kId = CSSPropertyID::kFont;
  struct Input {
    std::optional<css_longhand::FontStyle::Input> style;
    // <font-variant-css2>: normal | small-caps.
    std::optional<CSSValueID> variant_caps;
    std::optional<css_longhand::FontWeight::Input> weight;
    // <font-stretch-css3>: keywords only.
    std::optional<CSSValueID> stretch;
    // Required.
    css_longhand::FontSize::Input size;
    std::optional<css_longhand::LineHeight::Input> line_height;
    // Required.
    css_longhand::FontFamily::Input family;
  };
  static bool Make(const Input&, CSSPropertyValues& properties);
};

// FontSynthesis::ParseShorthand(): none, or the kinds of synthesis allowed
// (auto); the others are none.
struct FontSynthesis {
  static constexpr CSSPropertyID kId = CSSPropertyID::kFontSynthesis;
  // At least one.
  struct Kinds {
    bool weight = false;
    bool style = false;
    bool small_caps = false;
  };
  using Input = std::variant<CSSValueID, Kinds>;
  static bool Make(const Input&, CSSPropertyValues& properties);
};

// FontVariant::ParseShorthand(): normal | none (font-variant-ligatures, the
// others normal), or components of the longhands, the omitted ones normal.
struct FontVariant {
  static constexpr CSSPropertyID kId = CSSPropertyID::kFontVariant;
  // At least one component. The keyword lists are those of the longhands,
  // without normal and none.
  struct Components {
    Vector<CSSValueID> ligatures;
    // A caps keyword other than normal.
    std::optional<CSSValueID> caps;
    std::optional<css_longhand::FontVariantAlternates::Alternates> alternates;
    Vector<CSSValueID> numeric;
    Vector<CSSValueID> east_asian;
    // sub | super.
    std::optional<CSSValueID> position;
    // text | emoji | unicode.
    std::optional<CSSValueID> emoji;
  };
  using Input = std::variant<CSSValueID, Components>;
  static bool Make(const Input&, CSSPropertyValues& properties);
};

// TextDecoration::ParseShorthand() (ConsumeShorthandGreedilyViaLonghands()):
// at least one component; an omitted one is the initial value.
struct TextDecoration {
  static constexpr CSSPropertyID kId = CSSPropertyID::kTextDecoration;
  struct Input {
    std::optional<css_longhand::TextDecorationLine::Input> line;
    std::optional<css_longhand::TextDecorationThickness::Input> thickness;
    std::optional<CSSValueID> style;
    std::optional<StyleColorValue> color;
  };
  static bool Make(const Input&, CSSPropertyValues& properties);
};

// TextEmphasis::ParseShorthand() (ConsumeShorthandGreedilyViaLonghands()).
struct TextEmphasis {
  static constexpr CSSPropertyID kId = CSSPropertyID::kTextEmphasis;
  struct Input {
    std::optional<css_longhand::TextEmphasisStyle::Input> style;
    std::optional<StyleColorValue> color;
  };
  static bool Make(const Input&, CSSPropertyValues& properties);
};

// TextWrap::ParseShorthand() (ConsumeShorthandGreedilyViaLonghands()).
struct TextWrap {
  static constexpr CSSPropertyID kId = CSSPropertyID::kTextWrap;
  struct Input {
    std::optional<CSSValueID> mode;
    std::optional<CSSValueID> style;
  };
  static bool Make(const Input&, CSSPropertyValues& properties);
};

// WebkitTextStroke::ParseShorthand() (ConsumeShorthandGreedilyViaLonghands()).
struct WebkitTextStroke {
  static constexpr CSSPropertyID kId = CSSPropertyID::kWebkitTextStroke;
  struct Input {
    std::optional<css_longhand::WebkitTextStrokeWidth::Input> width;
    std::optional<StyleColorValue> color;
  };
  static bool Make(const Input&, CSSPropertyValues& properties);
};

// WhiteSpace::ParseShorthand().
struct WhiteSpace {
  static constexpr CSSPropertyID kId = CSSPropertyID::kWhiteSpace;
  // The multi-value syntax (ConsumeShorthandGreedilyViaLonghands()): at least
  // one component; an omitted one is the initial value.
  struct Longhands {
    std::optional<CSSValueID> collapse;
    std::optional<CSSValueID> wrap_mode;
  };
  // A predefined keyword (normal | nowrap | pre | pre-line | pre-wrap |
  // break-spaces) or the multi-value syntax. A single component that is also
  // a predefined keyword (break-spaces, nowrap) is that keyword, as the
  // parser tries the predefined keywords first.
  using Input = std::variant<CSSValueID, Longhands>;
  static bool Make(const Input&, CSSPropertyValues& properties);
};

} // namespace css_shorthand

} // namespace bkfont
