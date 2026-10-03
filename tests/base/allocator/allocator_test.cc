// Port exclusion: this source only tests STACK_ALLOCATED marker propagation.
// The stack-allocation annotation was removed from this extraction.
#if 0
// Port source: third_party/blink/renderer/platform/wtf/allocator/allocator_test.cc
// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/allocator/allocator.h"

#include <variant>

namespace {

struct Empty {};

struct StackAllocatedType {
  STACK_ALLOCATED();
};

static_assert(!blink::IsStackAllocatedTypeV<Empty>,
              "Failed to detect STACK_ALLOCATED macro.");
static_assert(blink::IsStackAllocatedTypeV<StackAllocatedType>,
              "Failed to detect STACK_ALLOCATED macro.");

static_assert(blink::IsStackAllocatedTypeV<std::pair<int, StackAllocatedType>>,
              "Failed to detect STACK_ALLOCATED macro.");
static_assert(blink::IsStackAllocatedTypeV<std::optional<StackAllocatedType>>,
              "Failed to detect STACK_ALLOCATED macro.");
static_assert(
    blink::IsStackAllocatedTypeV<std::variant<int, StackAllocatedType>>,
    "Failed to detect STACK_ALLOCATED macro.");

}  // namespace

#endif
