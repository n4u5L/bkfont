// Subset of blink/renderer/core/css/resolver/style_builder_converter.h for
// the ported longhands. There are no system fonts (CSSPendingSystemFontValue),
// tree scopes or visited-link colors.
#pragma once

#include <memory>

#include "base/text/atomic_string.h"
#include "font/font_description.h"
#include "geometry/length.h"
#include "style/computed_style_base_constants.h"
#include "style/computed_style_constants.h"
#include "style/css_identifier_value.h"
#include "style/css_value.h"
#include "style/css_value_list.h"
#include "style/shadow_list.h"
#include "style/style_color.h"
#include "style/style_hyphenate_limit_chars.h"
#include "style/text_decoration_thickness.h"
#include "text/tab_size.h"

namespace bkit {

class CSSToLengthConversionData;
class FontBuilder;
class StyleResolverState;

class StyleBuilderConverter {
public:
  StyleBuilderConverter() = delete;

  static FontDescription::FamilyDescription ConvertFontFamily(StyleResolverState&, const CSSValue&);
  static FontDescription::Kerning ConvertFontKerning(StyleResolverState&, const CSSValue&);
  static FontDescription::FontVariantPosition ConvertFontVariantPosition(StyleResolverState&, const CSSValue&);
  static FontVariantEmoji ConvertFontVariantEmoji(StyleResolverState&, const CSSValue&);
  static OpticalSizing ConvertFontOpticalSizing(StyleResolverState&, const CSSValue&);
  static std::shared_ptr<const FontFeatureSettings> ConvertFontFeatureSettings(StyleResolverState&, const CSSValue&);
  static std::shared_ptr<const FontVariationSettings> ConvertFontVariationSettings(StyleResolverState&,
                                                                                   const CSSValue&);
  static std::shared_ptr<const FontPalette> ConvertFontPalette(StyleResolverState&, const CSSValue&);
  static FontDescription::Size ConvertFontSize(StyleResolverState&, const CSSValue&);
  static FontSizeAdjust ConvertFontSizeAdjust(StyleResolverState&, const CSSValue&);
  static FontSelectionValue ConvertFontStretch(StyleResolverState&, const CSSValue&);
  static FontSelectionValue ConvertFontStyle(StyleResolverState&, const CSSValue&);
  static FontSelectionValue ConvertFontWeight(StyleResolverState&, const CSSValue&);
  static FontDescription::FontVariantCaps ConvertFontVariantCaps(StyleResolverState&, const CSSValue&);
  static FontDescription::VariantLigatures ConvertFontVariantLigatures(StyleResolverState&, const CSSValue&);
  static FontVariantNumeric ConvertFontVariantNumeric(StyleResolverState&, const CSSValue&);
  static FontVariantEastAsian ConvertFontVariantEastAsian(StyleResolverState&, const CSSValue&);
  static std::shared_ptr<const FontVariantAlternates> ConvertFontVariantAlternates(StyleResolverState&,
                                                                                   const CSSValue&);

  static StyleHyphenateLimitChars ConvertHyphenateLimitChars(StyleResolverState&, const CSSValue&);
  static Length ConvertLength(const StyleResolverState&, const CSSValue&);
  static Length ConvertLengthOrAuto(const StyleResolverState&, const CSSValue&);
  static TabSize ConvertLengthOrTabSpaces(StyleResolverState&, const CSSValue&);
  static Length ConvertLineHeight(StyleResolverState&, const CSSValue&);
  static Length ConvertSpacing(StyleResolverState&, const CSSValue&);
  static std::shared_ptr<const ShadowList> ConvertShadowList(StyleResolverState&, const CSSValue&);
  static ShadowData ConvertShadow(const CSSToLengthConversionData&, StyleResolverState*, const CSSValue&);
  static StyleColorValue ConvertStyleColor(StyleResolverState&, const CSSValue&);
  static TextDecorationThickness ConvertTextDecorationThickness(StyleResolverState&, const CSSValue&);
  static TextEmphasisPosition ConvertTextTextEmphasisPosition(StyleResolverState&, const CSSValue&);
  static float ConvertTextStrokeWidth(StyleResolverState&, const CSSValue&);
  static TextUnderlinePosition ConvertTextUnderlinePosition(StyleResolverState&, const CSSValue&);
  static Length ConvertTextUnderlineOffset(StyleResolverState&, const CSSValue&);
  // ConvertString<CSSValueID::kAuto>.
  static AtomicString ConvertStringOrAuto(StyleResolverState&, const CSSValue&);
  // ConvertLineWidth<float>.
  static float ConvertLineWidth(StyleResolverState&, const CSSValue&);

  template <typename T, CSSValueID ZeroValue = CSSValueID::kNone>
  static T ConvertFlags(StyleResolverState&, const CSSValue& value) {
    T flags = static_cast<T>(0);
    auto* identifier_value = DynamicTo<CSSIdentifierValue>(value);
    if (identifier_value && identifier_value->GetValueID() == ZeroValue) return flags;
    for (auto& flag_value : To<CSSValueList>(value)) flags |= To<CSSIdentifierValue>(*flag_value).ConvertTo<T>();
    return flags;
  }
};

} // namespace bkit
