// Ported from: blink/renderer/platform/fonts/opentype/color_table_lookup.h

// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "platform/typeface.h"

namespace bkfont {

class ColorTableLookup {
public:
  static bool TypefaceHasAnySupportedColorTable(const Typeface* typeface);
};

} // namespace bkfont
