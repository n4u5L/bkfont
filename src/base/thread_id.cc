// Ported from: skia/src/base/SkThreadID.cpp

#include "thread_id.h"

#ifdef _WIN32
#include "lean_windows.h"
#else
#include <pthread.h>
#endif

namespace bkfont {

ThreadID GetThreadID() {
#ifdef _WIN32
  return GetCurrentThreadId();
#else
  // pthread_t is an integer on Linux/Android and a pointer on Apple; the
  // C-style cast is the upstream conversion and covers both.
  return (ThreadID)pthread_self();
#endif
}

} // namespace bkfont
