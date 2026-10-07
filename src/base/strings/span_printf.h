// Ported from: chromium/base/strings/span_printf.h
// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file defines printf-like functions for working with span-based
// buffers.

#pragma once

#include <stdarg.h> // va_list

#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/strings/string_util.h"

namespace bkit::base {

// We separate the declaration from the implementation of this inline
// function just so the PRINTF_FORMAT works.
PRINTF_FORMAT(2, 0)
inline int VSpanPrintf(base::span<char> buffer,
                       const char* format,
                       va_list arguments);
inline int VSpanPrintf(base::span<char> buffer,
                       const char* format,
                       va_list arguments) {
  // SAFETY: buffer size obtained from span.
  return UNSAFE_BUFFERS(
      base::vsnprintf(buffer.data(), buffer.size(), format, arguments));
}

// We separate the declaration from the implementation of this inline
// function just so the PRINTF_FORMAT works.
PRINTF_FORMAT(2, 3)
inline int SpanPrintf(base::span<char> buffer, const char* format, ...);
inline int SpanPrintf(base::span<char> buffer, const char* format, ...) {
  va_list arguments;
  va_start(arguments, format);
  int result = VSpanPrintf(buffer, format, arguments);
  va_end(arguments);
  return result;
}

} // namespace bkit::base
