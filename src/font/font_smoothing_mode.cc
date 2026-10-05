// Ported from: blink/renderer/platform/fonts/font_smoothing_mode.cc
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "font_smoothing_mode.h"

#include "base/text/wtf_string.h"

namespace bkfont {

String ToString(FontSmoothingMode mode) {
  switch (mode) {
  case kAutoSmoothing:
    return "Auto";
  case kNoSmoothing:
    return "None";
  case kAntialiased:
    return "Antialiased";
  case kSubpixelAntialiased:
    return "SubpixelAntialiased";
  }
  return "Unknown";
}

} // namespace bkfont
