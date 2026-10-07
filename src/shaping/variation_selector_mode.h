// Ported from: blink/renderer/platform/fonts/shaping/variation_selector_mode.h
// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "font/font_variant_emoji.h"

namespace bkit {

enum VariationSelectorMode {
  // font-variant-emoji="normal". This value will behave as if
  // no variation selector was specified. This means that if no
  // variation selector is specified in the text or used for cmap
  // glyph lookup, fallback and glyph choice is controlled by the
  // order in which fonts are specified. If variation selectors
  // are specified in the text, perform cluster matching and take
  // the specified variation selector into account.
  kUseSpecifiedVariationSelector,
  // Ignore variation selectors mode. Fallback font selection
  // will only respect the order in which fonts are specified,
  // ignoring variation selectors in the text during cluster
  // matching.
  kIgnoreVariationSelector,
  // font-variant-emoji="text". Fallback font selection will act
  // as if variation selector 15 was added after each codepoint.
  kForceVariationSelector15,
  // font-variant-emoji="emoji". Fallback font selection will act
  // as if variation selector 16 was added after each codepoint.
  kForceVariationSelector16,
  // font-variant-emoji="unicode". Depending on the default unicode
  // presentation style of each codepoint in the text (i.e. text
  // default or emoji default), fallback font selection will act
  // as if variation selector 15 or variation selector 16 was added
  // after each codepoint correspondingly.
  kUseUnicodeDefaultPresentation
};

bool ShouldIgnoreVariationSelector(VariationSelectorMode mode);

bool UseFontVariantEmojiVariationSelector(
    VariationSelectorMode mode);

VariationSelectorMode
GetVariationSelectorModeFromFontVariantEmoji(
    FontVariantEmoji font_variant_emoji);

} // namespace bkit
