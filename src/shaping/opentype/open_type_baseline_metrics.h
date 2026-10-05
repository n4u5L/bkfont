// Ported from: blink/renderer/platform/fonts/opentype/open_type_baseline_metrics.h
// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hb.h>

#include <optional>

#include "font/font_description.h"

namespace bkfont {

class HarfBuzzFace;

class OpenTypeBaselineMetrics {
public:
  OpenTypeBaselineMetrics(HarfBuzzFace* harf_buzz_face,
                          FontOrientation orientation);

  // OpenType spec reference:
  // https://learn.microsoft.com/en-us/typography/opentype/spec/baselinetags
  // Read the alphabetic baseline from the open type table.
  std::optional<float> OpenTypeAlphabeticBaseline();
  // Read the hanging baseline from the open type table.
  std::optional<float> OpenTypeHangingBaseline();
  // Read the ideographic baseline from the open type table.
  std::optional<float> OpenTypeIdeographicBaseline();

private:
  // TODO(crbug.com/1489080): When this member was briefly given
  // MiraclePtr protection, it was found to be dangling.
  hb_font_t* font_;
  hb_direction_t hb_dir_;
};

} // namespace bkfont
