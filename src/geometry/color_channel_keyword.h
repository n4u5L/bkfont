// Ported from: blink/renderer/platform/geometry/color_channel_keyword.h
// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace bkit {

// Set of color channel keywords that can appear in a calc() expression.
enum class ColorChannelKeyword {
  kA,
  kB,
  kC,
  kG,
  kH,
  kL,
  kR,
  kS,
  kW,
  kX,
  kY,
  kZ,
  kAlpha
};

} // namespace bkit
