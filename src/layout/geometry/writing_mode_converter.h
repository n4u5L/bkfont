// Ported from: blink/renderer/core/layout/geometry/writing_mode_converter.{h,cc}
// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include <cassert>

#include "layout/geometry/logical_offset.h"
#include "layout/geometry/logical_size.h"
#include "layout/geometry/physical_rect.h"
#include "text/text_direction.h"
#include "text/writing_mode.h"

namespace bkit {

// The physical-to-logical subset used by inline hit testing. The two enums
// stand in for Blink's WritingDirectionMode; inner_size is zero for a point,
// 1x1 for a hit-test pixel, or the fragment size for a fragment's origin.
class WritingModeConverter {
public:
  WritingModeConverter(WritingMode writing_mode, TextDirection direction, PhysicalSize outer_size)
      : writing_mode_(writing_mode), direction_(direction), outer_size_(outer_size) {}

  LogicalOffset ToLogical(const PhysicalOffset& offset, const PhysicalSize& inner_size) const {
    const bool ltr = IsLtr(direction_);
    switch (writing_mode_) {
      case WritingMode::kHorizontalTb:
        return {ltr ? offset.left : outer_size_.width - offset.left - inner_size.width, offset.top};
      case WritingMode::kVerticalRl:
      case WritingMode::kSidewaysRl:
        return {ltr ? offset.top : outer_size_.height - offset.top - inner_size.height,
                outer_size_.width - offset.left - inner_size.width};
      case WritingMode::kVerticalLr:
        return {ltr ? offset.top : outer_size_.height - offset.top - inner_size.height, offset.left};
      case WritingMode::kSidewaysLr:
        return {ltr ? outer_size_.height - offset.top - inner_size.height : offset.top, offset.left};
    }
    assert(false);
    return {};
  }

  LogicalSize ToLogical(const PhysicalSize& size) const {
    return IsHorizontalWritingMode(writing_mode_) ? LogicalSize(size.width, size.height)
                                                 : LogicalSize(size.height, size.width);
  }

private:
  WritingMode writing_mode_;
  TextDirection direction_;
  PhysicalSize outer_size_;
};

} // namespace bkit
