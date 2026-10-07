// Ported from: blink/renderer/platform/fonts/canvas_rotation_in_vertical.h
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <cmath>
#include "base/vector.h"

namespace bkit {

enum class CanvasRotationInVertical : char {
  kRegular = 0,
  kRotateCanvasUpright = 1,
  kOblique = 2,
  kRotateCanvasUprightOblique = 3,
};

inline bool IsCanvasRotationInVerticalUpright(CanvasRotationInVertical r) {
  return static_cast<char>(r) & static_cast<char>(CanvasRotationInVertical::kRotateCanvasUpright);
}

inline bool IsCanvasRotationOblque(CanvasRotationInVertical r) {
  return static_cast<char>(r) & static_cast<char>(CanvasRotationInVertical::kOblique);
}

} // namespace bkit
