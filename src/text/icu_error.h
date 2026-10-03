// Source: third_party/blink/renderer/platform/text/icu_error.h
// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unicode/utypes.h>
#include "platform_export.h"
#include "base/allocator/allocator.h"

namespace blink {

// ICUError provides the unified way to handle ICU errors in Blink.
class ICUError {

public:
  ~ICUError() = default;

  UErrorCode* operator&() {
    return &error_;
  }
  operator UErrorCode() const {
    return error_;
  }
  operator UErrorCode&() {
    return error_;
  }

  void operator=(UErrorCode error) {
    error_ = error;
  }

private:
  UErrorCode error_ = U_ZERO_ERROR;
};

} // namespace blink
