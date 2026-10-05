// Ported from: blink/renderer/core/editing/bidi_adjustment.h
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include "layout/inline/inline_caret_position.h"

namespace bkfont {

InlineCaretPosition AdjustCaretForBidi(const InlineCaretPosition&);
InlineCaretPosition AdjustHitTestForBidi(const InlineCaretPosition&);

} // namespace bkfont
