// Source: third_party/blink/renderer/platform/wtf/text/string_utf8_adaptor.h
/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <string>
#include <string_view>

#include "base/containers/span.h"
#include "base/memory/raw_span.h"
#include "base/strings/string_view_util.h"
#include "base/text/string_view.h"
#include "base/wtf_size_t.h"

namespace bkfont {

// This class lets you get UTF-8 data out of a String without mallocing a
// separate buffer to hold the data if the String happens to be 8 bit and
// contain only ASCII characters.
class StringUtf8Adaptor final {

public:
  using iterator = base::raw_span<const char>::iterator;

  explicit StringUtf8Adaptor(
      StringView string,
      Utf8ConversionMode mode = Utf8ConversionMode::kLenient);
  ~StringUtf8Adaptor();

  const char* data() const {
    return span_.data();
  }
  wtf_size_t size() const {
    return span_.size();
  }

  // Iterators, so this type meets the requirements of
  // `std::ranges::contiguous_range`.
  iterator begin() const {
    return span_.begin();
  }
  iterator end() const {
    return span_.end();
  }

  std::string_view AsStringView() const {
    return base::as_string_view(span_);
  }

private:
  std::string utf8_buffer_;
  base::raw_span<const char> span_;
};

} // namespace bkfont
