// Ported from: blink/renderer/platform/geometry/physical_offset.h
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "layout/layout_unit.h"
#include "paint/geometry.h"

namespace bkit {

// PhysicalOffset is the position of a rect (typically a fragment) relative to
// its parent rect in the physical coordinate system.
// For more information about physical and logical coordinate systems, see:
// https://chromium.googlesource.com/chromium/src/+/main/third_party/blink/renderer/core/layout/README.md#coordinate-spaces
//
// Only the members used by the vertical text layout helpers are ported.
struct PhysicalOffset {
  constexpr PhysicalOffset() = default;
  constexpr PhysicalOffset(LayoutUnit left, LayoutUnit top)
      : left(left),
        top(top) {
  }

  // This is deleted to avoid unwanted lossy conversion from float or double to
  // LayoutUnit or int. Use explicit LayoutUnit constructor for each parameter,
  // or use FromPointF*() instead.
  PhysicalOffset(double, double) = delete;

  LayoutUnit left;
  LayoutUnit top;

  constexpr bool operator==(const PhysicalOffset& other) const = default;

  PhysicalOffset operator+(const PhysicalOffset& other) const {
    return PhysicalOffset{this->left + other.left, this->top + other.top};
  }
  PhysicalOffset& operator+=(const PhysicalOffset& other) {
    *this = *this + other;
    return *this;
  }

  PhysicalOffset operator-(const PhysicalOffset& other) const {
    return PhysicalOffset{this->left - other.left, this->top - other.top};
  }
  PhysicalOffset& operator-=(const PhysicalOffset& other) {
    *this = *this - other;
    return *this;
  }

  explicit PhysicalOffset(const Point& point)
      : left(point.x()),
        top(point.y()) {
  }
};

// TODO(crbug.com/41458361): These functions should upgraded to force correct
// pixel snapping in a type-safe way.
inline Point ToRoundedPoint(const PhysicalOffset& o) {
  return {o.left.Round(), o.top.Round()};
}
inline Point ToFlooredPoint(const PhysicalOffset& o) {
  return {o.left.Floor(), o.top.Floor()};
}
inline Point ToCeiledPoint(const PhysicalOffset& o) {
  return {o.left.Ceil(), o.top.Ceil()};
}

} // namespace bkit
