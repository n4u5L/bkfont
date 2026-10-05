// Ported from: chromium/base/threading/platform_thread.h

// Local implementation: native thread identifier adapter.
#pragma once
#include "build/build_config.h"
#if BUILDFLAG(IS_WIN)
#include <windows.h>
#else
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#endif
namespace bkfont::base {

#if BUILDFLAG(IS_WIN)
using PlatformThreadId = DWORD;
#else
using PlatformThreadId = pid_t;
#endif
class PlatformThread {
public:
  static PlatformThreadId CurrentId() {
#if BUILDFLAG(IS_WIN)
    return ::GetCurrentThreadId();
#else
    return static_cast<PlatformThreadId>(::syscall(SYS_gettid));
#endif
  }
};

} // namespace bkfont::base
