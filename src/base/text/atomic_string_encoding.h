// Ported from: blink/renderer/platform/wtf/text/atomic_string_encoding.h
// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace bkfont {

enum class AtomicStringUCharEncoding {
  kUnknown,

  // The string contains only 8-bit characters.
  kIs8Bit,

  // The string contains at least one 16-bit character.
  kIs16Bit,
};

} // namespace bkfont
