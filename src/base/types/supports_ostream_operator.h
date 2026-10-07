// Ported from: chromium/base/types/supports_ostream_operator.h
// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ostream>
#include <type_traits>
#include <utility>

namespace bkit::base::internal {

// Detects whether using operator<< would work.
//
// Note that the above #include of <ostream> is necessary to guarantee
// consistent results here for basic types.
template <typename T>
concept SupportsOstreamOperator =
    requires(const T& t, std::ostream& os) { os << t; };

} // namespace bkit::base::internal
