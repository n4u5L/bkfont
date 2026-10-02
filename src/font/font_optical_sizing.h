// Port source: third_party/blink/renderer/platform/fonts/font_optical_sizing.h
// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "wtf/text/wtf_string.h"

#include <cstdint>
#include "wtf/text/wtf_string.h"

namespace blink {
enum OpticalSizing {
  kAutoOpticalSizing,
  kNoneOpticalSizing
};

String ToString(OpticalSizing);
} // namespace blink
