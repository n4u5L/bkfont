// Ported from: blink/public/platform/web_font_render_style.h
// Ported from: blink/renderer/platform/fonts/web_font_render_style.cc
// Ported from: chromium/ui/gfx/font_render_params.h
// Ported from: chromium/ui/gfx/font_render_params_linux.cc

// Copyright 2014, 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "platform/platform_font.h"
#include "text_rendering_mode.h"

namespace bkfont {

// The resolved WebFontRenderStyle fields. Every platform starts with the
// gfx::FontRenderParams defaults, then applies Linux's device-scale and text
// rendering adjustments, without querying system preferences. There are no
// kNoPreference values after resolution.
struct FontRenderStyle {
  bool operator==(const FontRenderStyle&) const = default;

  // Resolve afresh so returning from a HiDPI display restores default hinting.
  // This same path is used for system fonts, custom fonts and resized copies.
  static FontRenderStyle Resolve(float device_scale_factor, TextRenderingMode text_rendering) {
    FontRenderStyle result;
    result.ApplyDeviceScaleFactor(device_scale_factor);
    // FontPlatformData::QuerySystemRenderStyle's Linux adjustment is applied
    // after the device policy, including at scale <= 1.
    if (text_rendering == kGeometricPrecision && result.use_anti_alias) {
      result.use_subpixel_positioning = true;
      result.use_hinting = false;
      result.hint_style = FontHinting::kNone;
    }
    return result;
  }

  // GetFontRenderParams' Linux adjustment, after resolving preferences and
  // before QuerySystemRenderStyle applies geometric-precision. The browser's
  // command-line overrides and ChromeOS policy are outside this port.
  void ApplyDeviceScaleFactor(float device_scale_factor) {
    if (!use_anti_alias) {
      use_hinting = true;
      hint_style = FontHinting::kFull;
      use_subpixel_rendering = false;
      use_subpixel_positioning = false;
    } else {
      use_subpixel_positioning = device_scale_factor > 1.0f;
      if (use_subpixel_positioning) {
        use_hinting = false;
        hint_style = FontHinting::kNone;
      }
    }
  }

  // WebFontRenderStyle::ApplyToSkFont, using the Linux rendering path on all
  // platforms. WebTestSupport overrides are outside this port.
  void ApplyToPlatformFont(PlatformFont* font) const {
    font->SetHinting(hint_style);
    font->SetEmbeddedBitmaps(use_bitmaps);
    font->SetForceAutoHinting(use_auto_hint);
    if (use_anti_alias && use_subpixel_rendering) {
      font->SetEdging(PlatformFont::Edging::kSubpixelAntiAlias);
    } else if (use_anti_alias) {
      font->SetEdging(PlatformFont::Edging::kAntiAlias);
    } else {
      font->SetEdging(PlatformFont::Edging::kAlias);
    }

    // Force-enable subpixel positioning, except when normal or full hinting
    // is requested. Linear metrics depend on the resolved preference itself.
    bool force_subpixel_positioning = hint_style < FontHinting::kNormal;
    font->SetSubpixel(force_subpixel_positioning || use_subpixel_positioning);
    font->SetLinearMetrics(use_subpixel_positioning);
  }

  bool use_bitmaps = false;
  bool use_auto_hint = false;
  bool use_hinting = true;
  FontHinting hint_style = FontHinting::kNormal;
  bool use_anti_alias = true;
  bool use_subpixel_rendering = false;
  bool use_subpixel_positioning = true;
};

} // namespace bkfont
