// Port source: third_party/blink/renderer/platform/fonts/font_custom_platform_data.cc
/*
 * Copyright (C) 2007 Apple Computer, Inc.
 * Copyright (c) 2007, 2008, 2009, Google Inc. All rights reserved.
 * Copyright (C) 2010 Company 100, Inc.
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

#include "font_custom_platform_data.h"

#include "font_cache.h"
#include "font_platform_data.h"
#include "font_format_check.h"
#include "font_settings.h"
#include "variable_axes_names.h"
#include "palette_interpolation.h"
#include "web_font_decoder.h"
#include "runtime_enabled_features.h"
#include "base/wtf_size_t.h"

#include <hb.h>
#include <memory>
#include <span>
namespace {
using namespace blink;

constexpr uint32_t kOpszTag = HB_TAG('o', 'p', 's', 'z');
constexpr uint32_t kSlntTag = HB_TAG('s', 'l', 'n', 't');
constexpr uint32_t kWdthTag = HB_TAG('w', 'd', 't', 'h');
constexpr uint32_t kWghtTag = HB_TAG('w', 'g', 'h', 't');

std::optional<FontVariationParameter> RetrieveVariationDesignParametersByTag(std::shared_ptr<FontFace> face, uint32_t tag) {
  for (const auto& axis : face->VariationParameters())
    if (axis.tag == tag) return axis;
  return std::nullopt;
}

} // namespace

namespace blink {

FontCustomPlatformData::FontCustomPlatformData(std::shared_ptr<FontFace> face, size_t data_size)
    : base_typeface_(std::move(face)),
      data_size_(data_size) {
}

FontCustomPlatformData::~FontCustomPlatformData() = default;

std::shared_ptr<const FontPlatformData> FontCustomPlatformData::GetFontPlatformData(
    float size,
    float adjusted_specified_size,
    bool bold,
    bool italic,
    const FontSelectionRequest& selection_request,
    const FontSelectionCapabilities& selection_capabilities,
    const OpticalSizing& optical_sizing,
    TextRenderingMode text_rendering,
    const ResolvedFontFeatures& resolved_font_features,
    FontOrientation orientation,
    const FontVariationSettings* variation_settings,
    const FontPalette* palette) const {

  std::shared_ptr<FontFace> return_typeface = base_typeface_;

  // Maximum axis count is maximum value for the OpenType USHORT,
  // which is a 16bit unsigned.
  // https://www.microsoft.com/typography/otspec/fvar.htm Variation
  // settings coming from CSS can have duplicate assignments and the
  // list can be longer than UINT16_MAX, but ignoring the length for
  // now, going with a reasonable upper limit. Deduplication is
  // handled by Skia with priority given to the last occuring
  // assignment.
  FontFormatCheck::VariableFontSubType font_sub_type =
      FontFormatCheck::ProbeVariableFont(base_typeface_);
  bool synthetic_bold = bold;
  bool synthetic_italic = italic;
  if (font_sub_type == FontFormatCheck::VariableFontSubType::kVariableTrueType || font_sub_type == FontFormatCheck::VariableFontSubType::kVariableCFF2) {
    // Three CSS axes, the accepted settings, and at most one implicit opsz.
    const size_t settings_count =
        variation_settings && variation_settings->size() < UINT16_MAX
            ? variation_settings->size()
            : 0;
    const size_t variation_capacity = 3 + settings_count + 1;
    auto variation = std::make_unique<PlatformFontVariationAxis[]>(variation_capacity);
    size_t variation_count = 0;

    PlatformFontVariationAxis weight_coordinate = {
        kWghtTag,
        static_cast<float>(selection_capabilities.weight.clampToRange(
            selection_request.weight))};
    std::optional<FontVariationParameter> wght_parameters =
        RetrieveVariationDesignParametersByTag(base_typeface_, kWghtTag);
    if (selection_capabilities.weight.IsRangeSetFromAuto() && wght_parameters) {
      FontSelectionRange wght_range = {
          FontSelectionValue(wght_parameters->minimum),
          FontSelectionValue(wght_parameters->maximum)};
      if (wght_range.IsValid()) {
        weight_coordinate = {
            kWghtTag,
            static_cast<float>(wght_range.clampToRange(selection_request.weight))};
        synthetic_bold = bold && wght_range.maximum < kBoldThreshold && selection_request.weight >= kBoldThreshold;
      }
    }

    PlatformFontVariationAxis width_coordinate = {
        kWdthTag,
        static_cast<float>(selection_capabilities.width.clampToRange(
            selection_request.width))};
    std::optional<FontVariationParameter> wdth_parameters =
        RetrieveVariationDesignParametersByTag(base_typeface_, kWdthTag);
    if (selection_capabilities.width.IsRangeSetFromAuto() && wdth_parameters) {
      FontSelectionRange wdth_range = {
          FontSelectionValue(wdth_parameters->minimum),
          FontSelectionValue(wdth_parameters->maximum)};
      if (wdth_range.IsValid()) {
        width_coordinate = {
            kWdthTag,
            static_cast<float>(wdth_range.clampToRange(selection_request.width))};
      }
    }
    // CSS and OpenType have opposite definitions of direction of slant
    // angle. In OpenType positive values turn counter-clockwise, negative
    // values clockwise - in CSS positive values are clockwise rotations /
    // skew. See note in https://drafts.csswg.org/css-fonts/#font-style-prop -
    // map value from CSS to OpenType here.
    PlatformFontVariationAxis slant_coordinate = {
        kSlntTag,
        static_cast<float>(-selection_capabilities.slope.clampToRange(
            selection_request.slope))};
    std::optional<FontVariationParameter> slnt_parameters =
        RetrieveVariationDesignParametersByTag(base_typeface_, kSlntTag);
    if (selection_capabilities.slope.IsRangeSetFromAuto() && slnt_parameters) {
      FontSelectionRange slnt_range = {
          FontSelectionValue(slnt_parameters->minimum),
          FontSelectionValue(slnt_parameters->maximum)};
      if (slnt_range.IsValid()) {
        slant_coordinate = {
            kSlntTag,
            static_cast<float>(slnt_range.clampToRange(-selection_request.slope))};
        synthetic_italic = italic && slnt_range.maximum < kItalicSlopeValue && selection_request.slope >= kItalicSlopeValue;
      }
    }

    variation[variation_count++] = weight_coordinate;
    variation[variation_count++] = width_coordinate;
    variation[variation_count++] = slant_coordinate;

    bool explicit_opsz_configured = false;
    if (variation_settings && variation_settings->size() < UINT16_MAX) {
      for (const auto& setting : *variation_settings) {
        if (setting.Tag() == kOpszTag)
          explicit_opsz_configured = true;
        PlatformFontVariationAxis setting_coordinate =
            {setting.Tag(), static_cast<float>(setting.Value())};
        variation[variation_count++] = setting_coordinate;
      }
    }

    if (!explicit_opsz_configured) {
      if (optical_sizing == kAutoOpticalSizing) {
        PlatformFontVariationAxis opsz_coordinate = {
            kOpszTag,
            static_cast<float>(adjusted_specified_size)};
        variation[variation_count++] = opsz_coordinate;
      } else if (optical_sizing == kNoneOpticalSizing) {
        // Explicitly set default value to avoid automatic application of
        // optical sizing as it seems to happen on SkTypeface on Mac.
        std::optional<FontVariationParameter> opsz_parameters =
            RetrieveVariationDesignParametersByTag(return_typeface, kOpszTag);
        if (opsz_parameters) {
          float opszDefault = opsz_parameters->default_value;
          PlatformFontVariationAxis opsz_coordinate = {
              kOpszTag,
              static_cast<float>(opszDefault)};
          variation[variation_count++] = opsz_coordinate;
        }
      }
    }

    auto variation_font = base_typeface_->WithVariations(std::span<const PlatformFontVariationAxis>(variation.get(), variation_count));
    if (variation_font) return_typeface = std::move(variation_font);
  }

  if (palette && !palette->IsNormalPalette()) {
    Vector<FontPalette::FontPaletteOverride> color_overrides;
    std::optional<uint16_t> palette_index;
    PaletteInterpolation interpolation(base_typeface_);
    if (palette->IsInterpolablePalette()) {
      color_overrides = interpolation.ComputeInterpolableFontPalette(palette);
      palette_index = 0;
    } else {
      color_overrides = *palette->GetColorOverrides();
      palette_index = interpolation.RetrievePaletteIndex(palette);
    }
    const size_t override_count = palette_index ? color_overrides.size() : 0;
    auto overrides = std::make_unique<PlatformPaletteOverride[]>(override_count);
    for (size_t i = 0; i < override_count; ++i) {
      const auto& entry = color_overrides[i];
      overrides[i] = {entry.index, entry.color.ToARGB32()};
    }
    auto palette_face = return_typeface->WithPalette(palette_index.value_or(0),
                                                     std::span<const PlatformPaletteOverride>(overrides.get(), override_count));
    if (palette_face) return_typeface = std::move(palette_face);
  }
  return std::make_shared<FontPlatformData>(
      std::move(return_typeface),
      String(),
      size,
      synthetic_bold && base_typeface_->Style().weight < kBoldThreshold,
      synthetic_italic && base_typeface_->Style().slant == FontSlant::kNormal,
      text_rendering,
      resolved_font_features,
      orientation);
}

Vector<VariationAxis> FontCustomPlatformData::GetVariationAxes() const {
  return VariableAxesNames::GetVariationAxes(base_typeface_);
}

String FontCustomPlatformData::FamilyNameForInspector() const {
  String result;
  for (const auto& name : base_typeface_->FamilyNames()) {
    result = name.name;
    if (name.locale == "en" || name.locale == "en-US") break;
  }
  return result;
}

String FontCustomPlatformData::GetPostScriptNameOrFamilyNameForInspector() const {
  String name = base_typeface_->PostScriptName();
  return name.IsNull() ? FamilyNameForInspector() : name;
}

std::shared_ptr<FontCustomPlatformData> FontCustomPlatformData::Create(
    std::span<const uint8_t> buffer,
    String& ots_parse_message) {

  WebFontDecoder decoder;
  std::shared_ptr<FontFace> typeface = decoder.Decode(buffer);
  if (!typeface) {
    ots_parse_message = decoder.GetErrorString();
    return nullptr;
  }
  return Create(std::move(typeface), decoder.DecodedSize());
}

std::shared_ptr<FontCustomPlatformData> FontCustomPlatformData::Create(
    std::shared_ptr<FontFace> typeface,
    size_t data_size) {
  return std::make_shared<FontCustomPlatformData>(
      std::move(typeface),
      data_size);
}

} // namespace blink
