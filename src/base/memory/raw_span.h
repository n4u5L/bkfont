// Local implementation: span alias without Chromium pointer instrumentation.
// Upstream reference: chromium/base/memory/raw_span.h
#pragma once
#include "base/containers/span.h"
namespace bkit::base {

template <typename T, std::size_t N = dynamic_extent>
using raw_span = span<T, N>;

} // namespace bkit::base
