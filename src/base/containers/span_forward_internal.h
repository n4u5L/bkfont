// Ported from: chromium/base/containers/span_forward_internal.h
// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>

#include <limits>

namespace bkit::base {

// [span.syn]: Constants
inline constexpr size_t dynamic_extent = std::numeric_limits<size_t>::max();

// [views.span]: class template `span<>`
template <typename ElementType,
          size_t Extent = dynamic_extent,
          // Storage pointer customization. By default this is not a
          // `raw_ptr<>`, since `span` is mostly used for stack variables. Use
          // `raw_span` instead for class fields, which sets this to
          // `raw_ptr<T>`.
          typename InternalPtrType = ElementType*>
class span;

} // namespace bkit::base
