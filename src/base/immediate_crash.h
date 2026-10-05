// Ported from: chromium/base/immediate_crash.h

// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The build has no C++ exceptions; unrecoverable failures trap immediately.
#pragma once
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace bkfont::base {

#if defined(_MSC_VER)
[[noreturn]] __forceinline void ImmediateCrash() {
  __debugbreak();
  __assume(0);
}
#else
[[noreturn]] __attribute__((always_inline)) inline void ImmediateCrash() {
#if defined(__i386__) || defined(__x86_64__)
  __asm__ __volatile__("int3; ud2");
#elif defined(__aarch64__)
  __asm__ __volatile__("brk #0; hlt #0");
#else
  __builtin_trap();
#endif
  __builtin_unreachable();
}
#endif

} // namespace bkfont::base
