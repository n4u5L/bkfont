// Adapted from: blink/renderer/platform/heap/collection_support/heap_vector.h
// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include "base/vector.h"

namespace bkit {

// Without Oilpan, HeapVector uses Vector's
// PartitionAllocator backing, inline capacity and ordinary element destruction.
// GC tracing, write barriers and GC-specific destruction traits are omitted.
template <typename T, wtf_size_t InlineCapacity = 0>
using HeapVector = Vector<T, InlineCapacity>;

} // namespace bkit
