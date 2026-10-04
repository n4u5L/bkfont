// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Source: base/immediate_crash.h. Only the MSVC x64 trap sequence is kept; the
// build is compiled without C++ exceptions, so unrecoverable failures crash.
#pragma once
#include <intrin.h>

namespace bkfont::base {

[[noreturn]] __forceinline void ImmediateCrash() {
  __debugbreak();
  __assume(0);
}

} // namespace bkfont::base
