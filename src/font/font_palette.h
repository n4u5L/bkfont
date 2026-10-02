// Port source: third_party/blink/renderer/platform/fonts/font_palette.h
// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <memory>
#include "graphics/color.h"
#include "wtf/text/atomic_string.h"

#include <cstdint>
#include "wtf/text/wtf_string.h"

namespace blink {

/* FontPalette stores CSS font-palette information in a
 * FontDescription. It's used for representing the computed style
 * information which can contain either light, dark or custom palette
 * information according to the font-palette property. */
class FontPalette {
public:
  enum KeywordPaletteName {
    kNormalPalette = 0,
    kLightPalette = 1,
    kDarkPalette = 2,
    kCustomPalette = 3,
    kInterpolablePalette = 4,
  };

  // Data layout should match SkFontArguments::PaletteOverride::ColorOverride.
  struct FontPaletteOverride {
    uint16_t index;
    Color color;

    bool operator==(const FontPaletteOverride& other) const {
      return index == other.index && color == other.color;
    }
  };

  enum BasePaletteValueType {
    kNoBasePalette,
    kLightBasePalette,
    kDarkBasePalette,
    kIndexBasePalette,
  };

  struct BasePaletteValue {
    BasePaletteValueType type;
    int index;

    bool hasValue() {
      return type != kNoBasePalette;
    }
    bool operator==(const BasePaletteValue& other) const {
      return type == other.type && index == other.index;
    }
  };

  struct NonNormalizedPercentages {
    double start;
    double end;
    bool operator==(const NonNormalizedPercentages& other) const {
      return start == other.start && end == other.end;
      ;
    }
  };

  static std::shared_ptr<FontPalette> Create() {
    return std::shared_ptr<FontPalette>(new FontPalette());
  }

  static std::shared_ptr<FontPalette> Create(KeywordPaletteName palette_name) {
    // Use AtomicString constructor for custom palette instantiation.

    return std::shared_ptr<FontPalette>(new FontPalette(palette_name));
  }

  static std::shared_ptr<FontPalette> Create(AtomicString palette_values_name) {
    return std::shared_ptr<FontPalette>(new FontPalette(std::move(palette_values_name)));
  }

  // We introduce a palette-mix() function to represent interpolated
  // font-palette values during animation/transition process, e.g. font-palette
  // property’s value at time 0.5 between the palettes “--p1” and “--p2” will be
  // presented as palette-mix(--p1, –p2, 0.5).
  static std::shared_ptr<FontPalette> Mix(
      std::shared_ptr<const FontPalette> start,
      std::shared_ptr<const FontPalette> end,
      double start_percentage,
      double end_percentage,
      double normalized_percentage,
      double alpha_multiplier,
      Color::ColorSpace color_interpolation_space,
      std::optional<Color::HueInterpolationMethod> hue_interpolation_method) {
    return std::shared_ptr<FontPalette>(new FontPalette(
        start,
        end,
        NonNormalizedPercentages{start_percentage, end_percentage},
        normalized_percentage,
        alpha_multiplier,
        color_interpolation_space,
        hue_interpolation_method));
  }

  void SetBasePalette(BasePaletteValue base_palette) {
    base_palette_ = base_palette;
  }

  void SetColorOverrides(Vector<FontPaletteOverride>&& overrides) {
    palette_overrides_ = overrides;
  }

  bool IsNormalPalette() const {
    return palette_keyword_ == kNormalPalette;
  }
  bool IsCustomPalette() const {
    return palette_keyword_ == kCustomPalette;
  }
  bool IsInterpolablePalette() const {
    return palette_keyword_ == kInterpolablePalette;
  }
  KeywordPaletteName GetPaletteNameKind() const {
    return palette_keyword_;
  }

  /* Returns the identifier of the @font-palette-values rule that should be
   * retrieved to complete the palette selection or palette override information
   * for this FontPalette object. */
  const AtomicString& GetPaletteValuesName() const {

    return palette_values_name_;
  }

  const Vector<FontPaletteOverride>* GetColorOverrides() const {
    return &palette_overrides_;
  }

  BasePaletteValue GetBasePalette() const {
    return base_palette_;
  }

  void SetMatchFamilyName(AtomicString family_name) {
    match_font_family_ = family_name;
  }

  AtomicString GetMatchFamilyName() {
    return match_font_family_;
  }

  std::shared_ptr<const FontPalette> GetStart() const {

    return start_;
  }

  std::shared_ptr<const FontPalette> GetEnd() const {

    return end_;
  }

  double GetStartPercentage() const {

    return percentages_.start;
  }

  double GetEndPercentage() const {

    return percentages_.end;
  }

  double GetNormalizedPercentage() const {

    return normalized_percentage_;
  }

  static NonNormalizedPercentages ComputeEndpointPercentagesFromNormalized(
      double normalized_percentage) {
    double end_percentage = normalized_percentage * 100.0;
    double start_percentage = 100.0 - end_percentage;
    return NonNormalizedPercentages{start_percentage, end_percentage};
  }

  double GetAlphaMultiplier() const {

    return alpha_multiplier_;
  }

  Color::ColorSpace GetColorInterpolationSpace() const {

    return color_interpolation_space_;
  }

  std::optional<Color::HueInterpolationMethod> GetHueInterpolationMethod()
      const {

    return hue_interpolation_method_;
  }

  String ToString() const;

  bool operator==(const FontPalette& other) const;
  bool operator!=(const FontPalette& other) const {
    return !(*this == other);
  }

  unsigned GetHash() const;

private:
  explicit FontPalette(KeywordPaletteName palette_name)
      : palette_keyword_(palette_name),
        base_palette_({kNoBasePalette, 0}) {
  }
  explicit FontPalette(AtomicString palette_values_name)
      : palette_keyword_(kCustomPalette),
        palette_values_name_(palette_values_name),
        base_palette_({kNoBasePalette, 0}) {
  }
  FontPalette(
      std::shared_ptr<const FontPalette> start,
      std::shared_ptr<const FontPalette> end,
      NonNormalizedPercentages percentages,
      double normalized_percentage,
      double alpha_multiplier,
      Color::ColorSpace color_interpoaltion_space,
      std::optional<Color::HueInterpolationMethod> hue_interpolation_method)
      : palette_keyword_(kInterpolablePalette),
        start_(start),
        end_(end),
        percentages_(percentages),
        normalized_percentage_(normalized_percentage),
        alpha_multiplier_(alpha_multiplier),
        color_interpolation_space_(color_interpoaltion_space),
        hue_interpolation_method_(hue_interpolation_method) {
  }
  FontPalette()
      : palette_keyword_(kNormalPalette),
        base_palette_({kNoBasePalette, 0}) {
  }

  KeywordPaletteName palette_keyword_;
  AtomicString palette_values_name_;
  BasePaletteValue base_palette_;
  AtomicString match_font_family_;
  Vector<FontPaletteOverride> palette_overrides_;
  std::shared_ptr<const FontPalette> start_;
  std::shared_ptr<const FontPalette> end_;
  NonNormalizedPercentages percentages_;
  double normalized_percentage_;
  double alpha_multiplier_;
  Color::ColorSpace color_interpolation_space_;
  std::optional<Color::HueInterpolationMethod> hue_interpolation_method_;
};

} // namespace blink
