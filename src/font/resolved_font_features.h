// Ported from: blink/renderer/platform/fonts/resolved_font_features.h
// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "shaping/font_features.h"
#include "base/vector.h"

#include <cstdint>
#include "base/text/wtf_string.h"

namespace bkfont {

class FontFeatureSettings;
using ResolvedFontFeatures = Vector<FontFeatureValue>;

ResolvedFontFeatures ResolveFontFeatureSettingsDescriptor(
    const FontFeatureSettings* existing_features_settings,
    const FontFeatureSettings* new_settings);

} // namespace bkfont
