// Ported from: blink/renderer/core/layout/geometry/logical_offset.h
// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include "layout/layout_unit.h"

namespace bkit {

struct LogicalOffset {
  constexpr LogicalOffset() = default;
  constexpr LogicalOffset(LayoutUnit inline_offset, LayoutUnit block_offset)
      : inline_offset(inline_offset), block_offset(block_offset) {}
  LogicalOffset(double, double) = delete;

  LayoutUnit inline_offset;
  LayoutUnit block_offset;
  constexpr bool operator==(const LogicalOffset&) const = default;
};

} // namespace bkit
