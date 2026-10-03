// Port source: third_party/blink/renderer/platform/fonts/font_metrics_override.h
// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <optional>

#include <cstdint>
#include "base/text/wtf_string.h"

namespace blink {

struct FontMetricsOverride {
  std::optional<float> ascent_override;
  std::optional<float> descent_override;
  std::optional<float> line_gap_override;
};

} // namespace blink
