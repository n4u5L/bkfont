// Ported from: blink/renderer/platform/geometry/physical_size.h
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "shaping/support/layout_unit.h"

namespace bkfont {

// PhysicalSize is the size of a rect (typically a fragment) in the physical
// coordinate system.
// For more information about physical and logical coordinate systems, see:
// https://chromium.googlesource.com/chromium/src/+/main/third_party/blink/renderer/core/layout/README.md#coordinate-spaces
//
// Only the members used by the vertical text layout helpers are ported.
struct PhysicalSize {
  constexpr PhysicalSize() = default;
  constexpr PhysicalSize(LayoutUnit width, LayoutUnit height)
      : width(width),
        height(height) {
  }

  // This is deleted to avoid unwanted lossy conversion from float or double to
  // LayoutUnit or int. Use explicit LayoutUnit constructor for each parameter,
  // or use FromSizeF*() instead.
  PhysicalSize(double, double) = delete;

  LayoutUnit width;
  LayoutUnit height;

  constexpr bool operator==(const PhysicalSize& other) const = default;

  PhysicalSize operator+(const PhysicalSize& other) const {
    return PhysicalSize{this->width + other.width, this->height + other.height};
  }
  PhysicalSize& operator+=(const PhysicalSize& other) {
    *this = *this + other;
    return *this;
  }

  PhysicalSize operator-() const {
    return PhysicalSize{-this->width, -this->height};
  }
  PhysicalSize operator-(const PhysicalSize& other) const {
    return PhysicalSize{this->width - other.width, this->height - other.height};
  }
  PhysicalSize& operator-=(const PhysicalSize& other) {
    *this = *this - other;
    return *this;
  }

  constexpr bool IsEmpty() const {
    return width == LayoutUnit() || height == LayoutUnit();
  }
};

} // namespace bkfont
