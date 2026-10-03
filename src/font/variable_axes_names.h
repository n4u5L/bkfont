// Port source: third_party/blink/renderer/platform/fonts/opentype/variable_axes_names.h
// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "base/text/wtf_string.h"

#include "platform/font_face.h"
#include "font_table_harfbuzz.h"
namespace blink {

struct VariationAxis {
  String tag;
  String name;
  double minValue;
  double maxValue;
  double defaultValue;
};

class VariableAxesNames {
public:
  static Vector<VariationAxis> GetVariationAxes(std::shared_ptr<FontFace> typeface);
};

} // namespace blink
