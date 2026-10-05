// Ported from: blink/renderer/platform/wtf/text/strcat.h
// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <initializer_list>

#include "base/containers/span.h"
#include "base/text/string_view.h"
#include "base/text/wtf_string.h"

namespace bkfont {

// StrCat is a function to perform concatenation on a sequence of strings.
// It is preferable to a sequence of "a + b + c" because it is both faster and
// generates less code.
//
//   String result = StrCat({"foo ", result, "\nfoo ", bar});
//
// It's a Blink-variant of base::StrCat() and absl::StrCat().
//
// StrCat is generally faster than operator+ and String::Format.
[[nodiscard]] String
StrCat(std::initializer_list<StringView> pieces);

} // namespace bkfont
