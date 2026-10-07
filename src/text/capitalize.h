// Ported from: blink/renderer/platform/text/capitalize.h
// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include "base/text/wtf_string.h"

namespace bkit {

String Capitalize(const String&, UChar previous_character);

} // namespace bkit
