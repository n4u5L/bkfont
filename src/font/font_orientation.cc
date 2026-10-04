// Port source: third_party/blink/renderer/platform/fonts/font_orientation.cc
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "font_orientation.h"

#include "base/text/wtf_string.h"

namespace bkfont {

String ToString(FontOrientation orientation) {
  switch (orientation) {
  case FontOrientation::kHorizontal:
    return "Horizontal";
  case FontOrientation::kVerticalRotated:
    return "VerticalRotated";
  case FontOrientation::kVerticalMixed:
    return "VerticalMixed";
  case FontOrientation::kVerticalUpright:
    return "VerticalUpright";
  }
  return "Unknown";
}

} // namespace bkfont
