// Ported from: blink/renderer/core/layout/geometry/logical_size.h
// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "layout/layout_unit.h"

namespace bkfont {

// LogicalSize is the size of rect (typically a fragment) in the logical
// coordinate system.
// For more information about physical and logical coordinate systems, see:
// https://chromium.googlesource.com/chromium/src/+/main/third_party/blink/renderer/core/layout/README.md#coordinate-spaces
//
// Only the members used by the vertical text layout helpers are ported.
struct LogicalSize {
  constexpr LogicalSize() = default;
  constexpr LogicalSize(LayoutUnit inline_size, LayoutUnit block_size)
      : inline_size(inline_size),
        block_size(block_size) {
  }

  // This is deleted to avoid unwanted lossy conversion from float or double to
  // LayoutUnit or int. Use explicit LayoutUnit constructor for each parameter
  // instead.
  LogicalSize(double, double) = delete;

  // Use ToPhysicalSize to convert to a physical size.

  LayoutUnit inline_size;
  LayoutUnit block_size;

  constexpr bool operator==(const LogicalSize& other) const = default;

  LogicalSize operator*(float scale) const {
    return LogicalSize(LayoutUnit(inline_size * scale), LayoutUnit(block_size * scale));
  }

  constexpr bool IsEmpty() const {
    return inline_size == LayoutUnit() || block_size == LayoutUnit();
  }
};

} // namespace bkfont
