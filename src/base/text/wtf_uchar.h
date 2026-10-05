// Ported from: blink/renderer/platform/wtf/text/wtf_uchar.h
// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#if defined(USING_SYSTEM_ICU)

#include <unicode/umachine.h> // IWYU pragma: export

#else

#include <stdint.h>

// These definitions should be matched to
// third_party/icu/source/common/unicode/umachine.h.
typedef char16_t UChar;
typedef int32_t UChar32;

#endif

;

namespace bkfont {

// Define platform neutral 8 bit character type (L is for Latin-1).
typedef unsigned char LChar;

} // namespace bkfont
