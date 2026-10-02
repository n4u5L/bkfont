#pragma once
#include "base/containers/span.h"
namespace base {
template <typename T, std::size_t N = dynamic_extent>
using raw_span = span<T, N>;
} // namespace base
