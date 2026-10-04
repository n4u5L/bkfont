#pragma once
#include "base/containers/span.h"
namespace bkfont::base {
template <typename T, std::size_t N = dynamic_extent>
using raw_span = span<T, N>;
} // namespace bkfont::base
