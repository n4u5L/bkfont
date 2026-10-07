// Ported from: blink/renderer/platform/wtf/text/math_transform.h
// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <unicode/umachine.h>

namespace bkit {
namespace unicode {

// Lookup the mathematical italic variant of a code point.
// https://w3c.github.io/mathml-core/#italic-mappings
UChar32 ItalicMathVariant(UChar32 code_point);

} // namespace unicode
} // namespace bkit
