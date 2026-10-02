// Ported from Chromium: third_party/blink/renderer/platform/fonts/small_caps_iterator.h
// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "base/containers/span.h"
#include <span>
#include "font/font_orientation.h"
#include "script_run_iterator.h"
#include "utf16_text_iterator.h"

namespace blink {

class SmallCapsIterator {

public:
  enum SmallCapsBehavior {
    kSmallCapsSameCase,
    kSmallCapsUppercaseNeeded,
    kSmallCapsInvalid
  };

  explicit SmallCapsIterator(base::span<const UChar> buffer);
  SmallCapsIterator(const SmallCapsIterator&) = delete;
  SmallCapsIterator& operator=(const SmallCapsIterator&) = delete;

  bool Consume(unsigned* caps_limit, SmallCapsBehavior*);

private:
  UTF16TextIterator utf16_iterator_;
  UChar32 next_u_char32_;
  bool at_end_;

  SmallCapsBehavior current_small_caps_behavior_;
  SmallCapsBehavior previous_small_caps_behavior_;
};

} // namespace blink
