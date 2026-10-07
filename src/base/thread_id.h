// Ported from: skia/include/private/base/SkThreadID.h

#pragma once

#include <cstdint>

namespace bkit {

using ThreadID = std::int64_t;

ThreadID GetThreadID();

constexpr ThreadID kIllegalThreadID = 0;

} // namespace bkit
