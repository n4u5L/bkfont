// Source: third_party/blink/renderer/platform/wtf/stack_util.h
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include "base/compiler_specific.h"
#include "build/build_config.h"
#include "wtf_export.h"

namespace blink {

size_t GetUnderestimatedStackSize();
void* GetStackStart();
bool IsOnStack(void* address);

// Returns the current stack position such that it works correctly with ASAN and
// SafeStack. Must be marked noinline because it relies on compiler intrinsics
// that report the current stack frame and if inlined it could report a position
// above the current stack position.
NOINLINE uintptr_t GetCurrentStackPosition();

namespace internal {

extern uintptr_t g_main_thread_stack_start;
extern uintptr_t g_main_thread_underestimated_stack_size;

void InitializeMainThreadStackEstimate();

#if BUILDFLAG(IS_WIN) && defined(COMPILER_MSVC)
size_t ThreadStackSize();
#endif

} // namespace internal

// Returns true if the function is not called on the main thread. May return
// false positives. Returning false guarantees execution on the main thread
// though.
ALWAYS_INLINE bool MayNotBeMainThread() {
  uintptr_t dummy;
  const uintptr_t address_diff =
      internal::g_main_thread_stack_start - reinterpret_cast<uintptr_t>(&dummy);
  // This is a fast way to judge if we are in the main thread. If |&dummy| is
  // within |g_main_thread_underestimated_stack_size| byte from the stack start
  // of the main thread, we judge that we are in the main thread.
  return address_diff >= internal::g_main_thread_underestimated_stack_size;
}

} // namespace blink
