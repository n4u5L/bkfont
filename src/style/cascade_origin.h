// Ported from: blink/renderer/core/css/resolver/cascade_origin.h
// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include <cstdint>

namespace bkfont {

// https://drafts.csswg.org/css-cascade/#cascade-origin
//
// The values are ordered by precedence. kNone is used for explicit defaults.
// Local resolution produces kUserAgent (host defaults on the root) and
// kAuthor (named rules and the node's declaration block).
enum class CascadeOrigin : uint8_t {
  kNone = 0,
  kUserAgent = 0b0001,
  kUser = 0b0010,
  kAuthorPresentationalHint = 0b0011,
  kAuthor = 0b0100,
  kAnimation = 0b0101,
  kTransition = 0b10000,
};

} // namespace bkfont
