// Ported from: blink/renderer/core/css/resolver/font_builder.h
/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */
//
// The Document is replaced by the StyleHostContext: its Settings and font
// selector. There are no tree scopes, text zoom, text autosizing or
// text-size-adjust.
#pragma once

#include <memory>

#include "font/font_description.h"
#include "style/font_size_functions.h"

namespace bkit {

class ComputedStyle;
class ComputedStyleBuilder;
class FontSelector;
class StyleHostContext;

class FontBuilder {
public:
  explicit FontBuilder(const StyleHostContext*);
  FontBuilder(const FontBuilder&) = delete;
  FontBuilder& operator=(const FontBuilder&) = delete;

  void DidChangeEffectiveZoom();
  void DidChangeTextOrientation();
  void DidChangeWritingMode();

  FontFamily StandardFontFamily() const;
  AtomicString StandardFontFamilyName() const;
  AtomicString GenericFontFamilyName(FontDescription::GenericFamilyType) const;

  float FontSizeForKeyword(unsigned keyword, bool is_monospace) const;

  void SetSize(const FontDescription::Size&);
  void SetSizeAdjust(const FontSizeAdjust&);

  void SetStretch(FontSelectionValue);
  void SetStyle(FontSelectionValue);
  void SetWeight(FontSelectionValue);

  void SetFamilyDescription(const FontDescription::FamilyDescription&);
  void SetFeatureSettings(std::shared_ptr<const FontFeatureSettings>);
  void SetLocale(scoped_refptr<const LayoutLocale>);
  void SetVariantCaps(FontDescription::FontVariantCaps);
  void SetVariantEastAsian(const FontVariantEastAsian);
  void SetVariantLigatures(const FontDescription::VariantLigatures&);
  void SetVariantNumeric(const FontVariantNumeric&);
  void SetFontSynthesisWeight(FontDescription::FontSynthesisWeight);
  void SetFontSynthesisStyle(FontDescription::FontSynthesisStyle);
  void SetFontSynthesisSmallCaps(FontDescription::FontSynthesisSmallCaps);
  void SetTextRendering(TextRenderingMode);
  void SetKerning(FontDescription::Kerning);
  void SetTextSpacingTrim(TextSpacingTrim);
  void SetFontPalette(std::shared_ptr<const FontPalette>);
  void SetFontVariantAlternates(std::shared_ptr<const FontVariantAlternates>);
  void SetFontOpticalSizing(OpticalSizing);
  void SetFontSmoothing(FontSmoothingMode);
  void SetVariationSettings(std::shared_ptr<const FontVariationSettings>);
  void SetVariantPosition(FontDescription::FontVariantPosition);
  void SetVariantEmoji(FontVariantEmoji);

  // UpdateFontDescription() returns true if any properties were actually
  // changed.
  bool UpdateFontDescription(FontDescription&, FontOrientation = FontOrientation::kHorizontal);
  void CreateFont(ComputedStyleBuilder&, const ComputedStyle* parent_style);
  void CreateInitialFont(ComputedStyleBuilder&);

  bool FontDirty() const { return flags_; }

  static FontDescription::FamilyDescription InitialFamilyDescription() {
    return FontDescription::FamilyDescription(InitialGenericFamily());
  }
  static std::shared_ptr<const FontFeatureSettings> InitialFeatureSettings() { return nullptr; }
  static std::shared_ptr<const FontVariationSettings> InitialVariationSettings() { return nullptr; }
  static std::shared_ptr<const FontPalette> InitialFontPalette() { return nullptr; }
  static std::shared_ptr<const FontVariantAlternates> InitialFontVariantAlternates() { return nullptr; }
  static FontDescription::GenericFamilyType InitialGenericFamily() { return FontDescription::kStandardFamily; }
  static FontDescription::Size InitialSize() {
    return FontDescription::Size(FontSizeFunctions::InitialKeywordSize(), 0.0f, false);
  }
  static FontSizeAdjust InitialSizeAdjust() { return FontSizeAdjust(); }
  static TextRenderingMode InitialTextRendering() { return kAutoTextRendering; }
  static FontDescription::FontVariantCaps InitialVariantCaps() { return FontDescription::kCapsNormal; }
  static FontVariantEastAsian InitialVariantEastAsian() { return FontVariantEastAsian(); }
  static FontDescription::VariantLigatures InitialVariantLigatures() { return FontDescription::VariantLigatures(); }
  static FontVariantNumeric InitialVariantNumeric() { return FontVariantNumeric(); }
  static const LayoutLocale* InitialLocale() { return nullptr; }
  static FontDescription::Kerning InitialKerning() { return FontDescription::kAutoKerning; }
  static TextSpacingTrim InitialTextSpacingTrim() { return TextSpacingTrim::kInitial; }
  static OpticalSizing InitialFontOpticalSizing() { return kAutoOpticalSizing; }
  static FontSmoothingMode InitialFontSmoothing() { return kAutoSmoothing; }

  static constexpr FontSelectionValue InitialStretch() { return kNormalWidthValue; }
  static constexpr FontSelectionValue InitialStyle() { return kNormalSlopeValue; }
  static constexpr FontSelectionValue InitialWeight() { return kNormalWeightValue; }
  static FontDescription::FontSynthesisWeight InitialFontSynthesisWeight() {
    return FontDescription::kAutoFontSynthesisWeight;
  }
  static FontDescription::FontSynthesisStyle InitialFontSynthesisStyle() {
    return FontDescription::kAutoFontSynthesisStyle;
  }
  static FontDescription::FontSynthesisSmallCaps InitialFontSynthesisSmallCaps() {
    return FontDescription::kAutoFontSynthesisSmallCaps;
  }
  static FontDescription::FontVariantPosition InitialVariantPosition() {
    return FontDescription::kNormalVariantPosition;
  }
  static FontVariantEmoji InitialVariantEmoji() { return kNormalVariantEmoji; }

private:
  void SetFamilyDescription(FontDescription&, const FontDescription::FamilyDescription&);
  void SetSize(FontDescription&, const FontDescription::Size&);
  // This function fixes up the default font size if it detects that the current
  // generic font family has changed. -dwh
  void CheckForGenericFamilyChange(const FontDescription&, FontDescription&);
  void UpdateSpecifiedSize(FontDescription&, const FontDescription& parent_description);
  void UpdateComputedSize(FontDescription&, const ComputedStyleBuilder&);
  void UpdateAdjustedSize(FontDescription&, const std::shared_ptr<FontSelector>&);

  float GetComputedSizeFromSpecifiedSize(const FontDescription&, const ComputedStyleBuilder&, float specified_size);

  std::shared_ptr<FontSelector> ComputeFontSelector(const ComputedStyleBuilder&);

  const StyleHostContext* host_;
  FontDescription font_description_;

  enum class PropertySetFlag {
    kWeight,
    kSize,
    kStretch,
    kFamily,
    kFeatureSettings,
    kLocale,
    kStyle,
    kSizeAdjust,
    kVariantCaps,
    kVariantEastAsian,
    kVariantLigatures,
    kVariantNumeric,
    kVariantEmoji,
    kVariantPosition,
    kVariationSettings,
    kTextRendering,
    kKerning,
    kTextSpacingTrim,
    kFontOpticalSizing,
    kFontPalette,
    kFontVariantAlternates,
    kFontSmoothing,
    kFontSynthesisWeight,
    kFontSynthesisStyle,
    kFontSynthesisSmallCaps,

    kEffectiveZoom,
    kTextOrientation,
    kWritingMode,

    kNumFlags,
  };

  void Set(PropertySetFlag flag) { flags_ |= (1 << unsigned(flag)); }
  bool IsSet(PropertySetFlag flag) const { return flags_ & (1 << unsigned(flag)); }

  unsigned flags_{0};
  static_assert(static_cast<int>(PropertySetFlag::kNumFlags) <= sizeof(flags_) * 8);
};

} // namespace bkit
