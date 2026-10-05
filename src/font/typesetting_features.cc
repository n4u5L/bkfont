// Ported from: blink/renderer/platform/fonts/typesetting_features.cc
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typesetting_features.h"

#include <array>

#include "base/text/string_builder.h"
#include "base/text/wtf_string.h"

namespace bkfont {

namespace {

std::array<const char*, kMaxTypesettingFeatureIndex + 1> kFeatureNames = {
    "Kerning",
    "Ligatures",
    "Caps"};

} // namespace

String ToString(TypesettingFeatures features) {
  StringBuilder builder;
  int featureCount = 0;
  for (int i = 0; i <= kMaxTypesettingFeatureIndex; i++) {
    if (features & (1 << i)) {
      if (featureCount++ > 0)
        builder.Append(",");
      builder.Append(kFeatureNames[i]);
    }
  }
  return builder.ToString();
}

} // namespace bkfont
