// Ported from: blink/renderer/core/layout/geometry/physical_rect.h
// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "geometry/physical_offset.h"
#include "geometry/physical_size.h"
#include "shaping/support/layout_unit.h"

namespace bkfont {

// PhysicalRect is the position and size of a rect (typically a fragment)
// relative to its parent rect in the physical coordinate system.
// For more information about physical and logical coordinate systems, see:
// https://chromium.googlesource.com/chromium/src/+/main/third_party/blink/renderer/core/layout/README.md#coordinate-spaces
//
// Only the members used by the vertical text layout helpers are ported.
struct PhysicalRect {
  constexpr PhysicalRect() = default;
  constexpr PhysicalRect(const PhysicalOffset& offset, const PhysicalSize& size)
      : offset(offset),
        size(size) {
  }
  constexpr PhysicalRect(LayoutUnit left, LayoutUnit top, LayoutUnit width, LayoutUnit height)
      : offset(left, top),
        size(width, height) {
  }

  // This is deleted to avoid unwanted lossy conversion from float or double to
  // LayoutUnit or int. Use explicit LayoutUnit constructor for each parameter,
  // or use EnclosingRect() or FastAndLossyFromRectF() instead.
  PhysicalRect(double, double, double, double) = delete;

  PhysicalOffset offset;
  PhysicalSize size;

  constexpr bool IsEmpty() const {
    return size.IsEmpty();
  }

  constexpr LayoutUnit X() const {
    return offset.left;
  }
  constexpr LayoutUnit Y() const {
    return offset.top;
  }
  constexpr LayoutUnit Width() const {
    return size.width;
  }
  constexpr LayoutUnit Height() const {
    return size.height;
  }
  LayoutUnit Right() const {
    return offset.left + size.width;
  }
  LayoutUnit Bottom() const {
    return offset.top + size.height;
  }

  void SetWidth(LayoutUnit w) {
    size.width = w;
  }
  void SetHeight(LayoutUnit h) {
    size.height = h;
  }

  constexpr bool operator==(const PhysicalRect& other) const = default;

  void Move(const PhysicalOffset& o) {
    offset += o;
  }
};

} // namespace bkfont
