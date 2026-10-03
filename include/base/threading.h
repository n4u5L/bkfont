// Source: third_party/blink/renderer/platform/wtf/threading.h
/*
 * Copyright (C) 2007, 2008, 2010 Apple Inc. All rights reserved.
 * Copyright (C) 2007 Justin Haygood (jhaygood@reaktix.com)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <stdint.h>
#include <memory>

#include "base/threading/platform_thread.h"
#include "build/build_config.h"
#include "thread_specific.h"
#include "type_traits.h"

namespace blink {

#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_WIN)
base::PlatformThreadId CurrentThread();
#else
// On Android gettid(3) uses a faster TLS model than thread_local.
// On Windows GetCurrentThreadId() directly pick TID from TEB.
inline base::PlatformThreadId CurrentThread() {
  return base::PlatformThread::CurrentId();
}
#endif // !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_WIN)

#if 0
 bool IsBeforeThreadCreated();
 void WillCreateThread();
 void SetIsBeforeThreadCreatedForTest();
#endif

class Threading {

public:
  Threading();
  Threading(const Threading&) = delete;
  Threading& operator=(const Threading&) = delete;
  ~Threading();

  base::PlatformThreadId ThreadId() const {
    return thread_id_;
  }

  // Must be called on the main thread before any callers to wtfThreadData().
  static void Initialize();

#if BUILDFLAG(IS_WIN) && defined(COMPILER_MSVC)
  static size_t ThreadStackSize();
#endif

private:
  base::PlatformThreadId thread_id_;

#if BUILDFLAG(IS_WIN) && defined(COMPILER_MSVC)
  size_t thread_stack_size_ = 0u;
#endif

  static ThreadSpecific<Threading>* static_data_;
  friend Threading& BaseThreading();
};

inline Threading& BaseThreading() {
  ;
  return **Threading::static_data_;
}

} // namespace blink
