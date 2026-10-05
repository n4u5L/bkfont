// Ported from: blink/renderer/platform/geometry/blend.h
// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "shaping/support/layout_unit.h"
#include "platform_export.h"
#include "base/math_extras.h"
#include "paint/geometry.h"

#include <type_traits>

namespace bkfont {

inline int Blend(int from, int to, double progress) {
  return static_cast<int>(lround(from + (to - from) * progress));
}

// For unsigned types.
template <typename T>
inline T Blend(T from, T to, double progress) {
  ;
  return ClampTo<T>(round(to > from ? from + (to - from) * progress
                                    : from - (from - to) * progress));
}

inline double Blend(double from, double to, double progress) {
  return from + (to - from) * progress;
}

inline float Blend(float from, float to, double progress) {
  return static_cast<float>(from + (to - from) * progress);
}

inline LayoutUnit Blend(LayoutUnit from, LayoutUnit to, double progress) {
  return LayoutUnit(from + (to - from) * progress);
}

inline PointF Blend(const PointF& from,
                         const PointF& to,
                         double progress) {
  return PointF(Blend(from.x(), to.x(), progress),
                     Blend(from.y(), to.y(), progress));
}

} // namespace bkfont
