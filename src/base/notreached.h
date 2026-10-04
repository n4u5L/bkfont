// Ported from: base/notreached.h

#pragma once

#include <source_location>

#include "compiler_specific.h"

namespace bkfont::logging {

// NOTREACHED() annotates should-be unreachable code. It is fatal in every
// build: it writes "<file>(<line>): fatal error: "NOTREACHED hit."" to stderr
// and terminates. NotFatalUntil milestones and DUMP_WILL_BE_NOTREACHED() are
// not ported.
[[noreturn]] NOINLINE void NotReachedFailure(std::source_location location = std::source_location::current());

} // namespace bkfont::logging

#define NOTREACHED() ::bkfont::logging::NotReachedFailure()
