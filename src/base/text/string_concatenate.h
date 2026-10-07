// Ported from: blink/renderer/platform/wtf/text/string_concatenate.h
/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <string_view>

#include "base/containers/span.h"
#include "base/allocator/allocator.h"
#include "base/text/string_view.h"

namespace bkit {

template <typename StringType>
class StringTypeAdapter {
};

template <>
class StringTypeAdapter<char> {

public:
  explicit StringTypeAdapter(char buffer)
      : buffer_(buffer) {
  }

  size_t length() const {
    return 1;
  }
  bool Is8Bit() const {
    return true;
  }

  void WriteTo(base::span<LChar> destination) const {
    destination[0] = buffer_;
  }
  void WriteTo(base::span<UChar> destination) const {
    destination[0] = buffer_;
  }

private:
  const LChar buffer_;
};

template <>
class StringTypeAdapter<LChar> : public StringTypeAdapter<char> {
public:
  explicit StringTypeAdapter(LChar buffer)
      : StringTypeAdapter<char>(buffer) {
  }
};

template <>
class StringTypeAdapter<UChar> {

public:
  explicit StringTypeAdapter(UChar buffer)
      : buffer_(buffer) {
  }

  size_t length() const {
    return 1;
  }
  bool Is8Bit() const {
    return buffer_ <= 0xff;
  }

  void WriteTo(base::span<LChar> destination) const {
    ;
    destination[0] = static_cast<LChar>(buffer_);
  }

  void WriteTo(base::span<UChar> destination) const {
    destination[0] = buffer_;
  }

private:
  const UChar buffer_;
};

template <>
class StringTypeAdapter<const char*> {

public:
  explicit StringTypeAdapter(const char* buffer)
      : buffer_(base::as_byte_span(std::string_view(buffer))) {
  }

  size_t length() const {
    return buffer_.size();
  }
  bool Is8Bit() const {
    return true;
  }

  void WriteTo(base::span<LChar> destination) const;
  void WriteTo(base::span<UChar> destination) const;

private:
  const base::span<const LChar> buffer_;
};

template <>
class StringTypeAdapter<const LChar*>
    : StringTypeAdapter<const char*> {
public:
  explicit StringTypeAdapter(const LChar* buffer)
      : StringTypeAdapter<const char*>(reinterpret_cast<const char*>(buffer)) {
  }
};

template <>
class StringTypeAdapter<char*>
    : public StringTypeAdapter<const char*> {
public:
  explicit StringTypeAdapter(char* buffer)
      : StringTypeAdapter<const char*>(buffer) {
  }
};

template <>
class StringTypeAdapter<LChar*>
    : public StringTypeAdapter<const LChar*> {
public:
  explicit StringTypeAdapter(LChar* buffer)
      : StringTypeAdapter<const LChar*>(buffer) {
  }
};

template <>
class StringTypeAdapter<const UChar*> {

public:
  explicit StringTypeAdapter(const UChar* buffer);

  size_t length() const {
    return buffer_.size();
  }
  bool Is8Bit() const {
    return false;
  }

  void WriteTo(base::span<LChar> destination) const {
    ;
  }
  void WriteTo(base::span<UChar> destination) const;

private:
  const base::span<const UChar> buffer_;
};

template <>
class StringTypeAdapter<StringView> {

public:
  explicit StringTypeAdapter(const StringView& view)
      : view_(view) {
  }

  size_t length() const {
    return view_.length();
  }
  bool Is8Bit() const {
    return view_.Is8Bit();
  }

  void WriteTo(base::span<LChar> destination) const;
  void WriteTo(base::span<UChar> destination) const;

private:
  const StringView view_;
};

} // namespace bkit
