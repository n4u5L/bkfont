// Ported from: chromium/base/check.cc

#include "notreached.h"

#include <cstdio>

#if defined(_WIN32)
#include <intrin.h>

#include "lean_windows.h"
#endif

namespace bkfont::logging {

// NotReachedNoreturnError::~NotReachedNoreturnError. The message is flushed
// before terminating. This function ends up in crash stack traces.
void NotReachedFailure(std::source_location location) {
  std::fprintf(stderr, "%s(%u): fatal error: \"NOTREACHED hit.\"\n", location.file_name(), static_cast<unsigned>(location.line()));
  std::fflush(stderr);

#if defined(_WIN32)
  __fastfail(FAST_FAIL_FATAL_APP_EXIT);
#else
  __builtin_trap();
#endif
}

} // namespace bkfont::logging
