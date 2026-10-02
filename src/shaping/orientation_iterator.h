// Ported from Chromium: third_party/blink/renderer/platform/fonts/orientation_iterator.h
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

class OrientationIterator {

public:
  enum RenderOrientation {
    kOrientationKeep,
    kOrientationRotateSideways,
    kOrientationInvalid,

    // When adding values, ensure `kMaxEnumValue` is the largest value to store
    // (values that can be returned for non-empty inputs).
    kMaxEnumValue = kOrientationRotateSideways,
  };

  OrientationIterator(base::span<const UChar> buffer,
                      FontOrientation run_orientation);
  OrientationIterator(const OrientationIterator&) = delete;
  OrientationIterator& operator=(const OrientationIterator&) = delete;

  bool Consume(unsigned* orientation_limit, RenderOrientation*);

private:
  UTF16TextIterator utf16_iterator_;
  bool at_end_;
};

} // namespace blink
