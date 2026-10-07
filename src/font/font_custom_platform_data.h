// Ported from: blink/renderer/platform/fonts/font_custom_platform_data.h
/*
 * Copyright (C) 2007 Apple Computer, Inc.
 * Copyright (c) 2007, 2008, 2009, Google Inc. All rights reserved.
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

#pragma once

#include "font_optical_sizing.h"
#include "font_orientation.h"
#include "font_palette.h"
#include "font_selection_types.h"
#include "variable_axes_names.h"
#include "resolved_font_features.h"
#include "text_rendering_mode.h"
#include "base/forward.h"
#include "base/text/wtf_string.h"

#include <span>
#include "platform/typeface.h"
namespace bkit {

class FontPlatformData;
class FontVariationSettings;

class FontCustomPlatformData {
public:
  static std::shared_ptr<FontCustomPlatformData> Create(std::span<const uint8_t>,
                                                        String& ots_parse_message);
  static std::shared_ptr<FontCustomPlatformData> Create(std::shared_ptr<Typeface>, size_t data_size);

  FontCustomPlatformData(std::shared_ptr<Typeface>, size_t data_size);
  FontCustomPlatformData(const FontCustomPlatformData&) = delete;
  FontCustomPlatformData& operator=(const FontCustomPlatformData&) = delete;
  ~FontCustomPlatformData();

  // The size argument should come from EffectiveFontSize() and
  // adjusted_specified_size should come from AdjustedSpecifiedSize() of
  // FontDescription. The latter is needed for correctly applying
  // font-optical-sizing: auto; independent of zoom level.
  std::shared_ptr<const FontPlatformData> GetFontPlatformData(
      float size,
      float adjusted_specified_size,
      bool bold,
      bool italic,
      const FontSelectionRequest&,
      const FontSelectionCapabilities&,
      const OpticalSizing& optical_sizing,
      TextRenderingMode text_rendering,
      const ResolvedFontFeatures& resolved_font_features,
      FontOrientation = FontOrientation::kHorizontal,
      const FontVariationSettings* = nullptr,
      const FontPalette* = nullptr) const;

  String FamilyNameForInspector() const;

  String GetPostScriptNameOrFamilyNameForInspector() const;

  Vector<VariationAxis> GetVariationAxes() const;

  size_t DataSize() const {
    return data_size_;
  }

private:
  std::shared_ptr<Typeface> base_typeface_;
  size_t data_size_;
};

} // namespace bkit
