// Ported from: blink/renderer/platform/fonts/opentype/open_type_features.h
// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <algorithm>
#include <hb.h>

#include "base/vector.h"

namespace bkit {

class SimpleFontData;

//
// Represents OpenType features in a font.
//
class OpenTypeFeatures {

public:
  explicit OpenTypeFeatures(const SimpleFontData& font);

  bool Contains(hb_tag_t feature_tag) const {
    return std::find(features_.begin(), features_.end(), feature_tag) != features_.end();
  }

private:
  // This value is heuristic, 64 is enough to load all features of "Yu Gothic".
  constexpr static unsigned kInitialSize = 64;

  Vector<hb_tag_t, kInitialSize> features_;
};

} // namespace bkit
