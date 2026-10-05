// Ported from: blink/public/platform/web_font_render_style.h
// Ported from: blink/renderer/platform/fonts/web_font_render_style.cc
// Ported from: chromium/ui/gfx/font_render_params.h

// Copyright 2014, 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "platform/platform_font.h"

namespace bkfont {

// The resolved WebFontRenderStyle fields. Every platform uses the defaults
// from gfx::FontRenderParams, without system preference queries or the
// browser's global setters. There are no kNoPreference values after resolution.
struct FontRenderStyle {
  bool operator==(const FontRenderStyle&) const = default;

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
