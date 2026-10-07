// Subset of blink/renderer/core/css/resolver/cascade_priority.h.
// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
//
// Local declarations have no !important, tree scopes, cascade layers or try
// styles, so those bits are always zero and only the origin, the position in
// the MatchResult and the apply generation remain. Comparison is unchanged:
// origin first, then position; the generation never decides the winner
// because every declaration is added with generation zero.
#pragma once

#include <cassert>
#include <cstdint>

#include "style/cascade_origin.h"

namespace bkit {

class CascadePriority {
public:
  static constexpr uint64_t kOriginImportanceOffset = 16; // of high_bits_
  static constexpr uint64_t kPositionOffset = 4;          // of low_bits_
  static constexpr uint64_t kPositionMask = static_cast<uint64_t>(0xFFFFFFFF) << kPositionOffset;
  static constexpr uint64_t kGenerationMask = 0xF;

  CascadePriority() : low_bits_(0), high_bits_(0) {}
  explicit CascadePriority(CascadeOrigin origin) : CascadePriority(origin, 0) {}
  CascadePriority(CascadeOrigin origin, uint32_t position)
      : CascadePriority(static_cast<uint64_t>(position) << kPositionOffset,
                        static_cast<uint32_t>(origin) << kOriginImportanceOffset) {}
  CascadePriority(CascadePriority o, uint8_t generation)
      : CascadePriority((o.low_bits_ & ~kGenerationMask) | generation, o.high_bits_) {
    assert(generation <= kGenerationMask);
  }

  CascadeOrigin GetOrigin() const { return static_cast<CascadeOrigin>(high_bits_ >> kOriginImportanceOffset); }
  bool HasOrigin() const { return GetOrigin() != CascadeOrigin::kNone; }
  uint32_t GetPosition() const { return static_cast<uint32_t>((low_bits_ & kPositionMask) >> kPositionOffset); }
  uint8_t GetGeneration() const { return static_cast<uint8_t>(low_bits_ & kGenerationMask); }

  bool operator>=(const CascadePriority& o) const {
    return high_bits_ > o.high_bits_ || (high_bits_ == o.high_bits_ && low_bits_ >= o.low_bits_);
  }
  bool operator<(const CascadePriority& o) const {
    return high_bits_ < o.high_bits_ || (high_bits_ == o.high_bits_ && low_bits_ < o.low_bits_);
  }
  bool operator==(const CascadePriority& o) const { return high_bits_ == o.high_bits_ && low_bits_ == o.low_bits_; }

private:
  CascadePriority(uint64_t low_bits, uint32_t high_bits) : low_bits_(low_bits), high_bits_(high_bits) {}

  uint64_t low_bits_;
  uint32_t high_bits_;
};

} // namespace bkit
