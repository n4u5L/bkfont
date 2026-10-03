// Ported from Chromium: third_party/blink/renderer/platform/fonts/shaping/case_mapping_harfbuzz_buffer_filler.h
// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "base/containers/span.h"
#include "base/text/atomic_string.h"
#include "base/text/wtf_string.h"
#include "base/text/wtf_uchar.h"

#include <hb.h>

namespace blink {

enum class CaseMapIntend {
  kKeepSameCase,
  kUpperCase,
  kLowerCase
};

class CaseMappingHarfBuzzBufferFiller {

public:
  CaseMappingHarfBuzzBufferFiller(CaseMapIntend,
                                  const AtomicString& locale,
                                  hb_buffer_t* harfbuzz_buffer,
                                  const String& text,
                                  unsigned start_index,
                                  unsigned num_characters);

private:
  void FillSlowCase(CaseMapIntend,
                    const AtomicString& locale,
                    base::span<const UChar> buffer,
                    unsigned start_index,
                    unsigned num_characters);
  hb_buffer_t* harfbuzz_buffer_;
};

} // namespace blink
