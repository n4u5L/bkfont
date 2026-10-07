// Ported from: blink/renderer/platform/fonts/font_optical_sizing.cc
// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "font_optical_sizing.h"

namespace bkit {

String ToString(OpticalSizing font_optical_sizing) {
  switch (font_optical_sizing) {
  case OpticalSizing::kAutoOpticalSizing:
    return "Auto";
  case OpticalSizing::kNoneOpticalSizing:
    return "None";
  }
  return "Unknown";
}

} // namespace bkit
