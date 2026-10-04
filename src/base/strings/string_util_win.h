// Source: base/strings/string_util_win.h
// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include <string>
#include <string_view>
#include <vector>

#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/strings/string_util.h"

namespace bkfont::base {

// Chromium code style is to not use malloc'd strings; this is only for use
// for interaction with APIs that require it.
inline char* strdup(const char* str) {
  return _strdup(str);
}

inline int vsnprintf(char* buffer,
                     size_t size,
                     const char* format,
                     va_list arguments) {
  int length =
      UNSAFE_TODO(vsnprintf_s(buffer, size, size - 1, format, arguments));
  if (length < 0) {
    return _vscprintf(format, arguments);
  }
  return length;
}

inline int vswprintf(wchar_t* buffer,
                     size_t size,
                     const wchar_t* format,
                     va_list arguments) {
  ;

  int length = _vsnwprintf_s(buffer, size, size - 1, format, arguments);
  if (length < 0) {
    return _vscwprintf(format, arguments);
  }
  return length;
}

// Utility functions to access the underlying string buffer as a wide char
// pointer.
//
// Note: These functions violate strict aliasing when char16_t and wchar_t are
// unrelated types. We thus pass -fno-strict-aliasing to the compiler on
// non-Windows platforms [1], and rely on it being off in Clang's CL mode [2].
//
// [1] https://crrev.com/b9a0976622/build/config/compiler/BUILD.gn#244
// [2]
// https://github.com/llvm/llvm-project/blob/1e28a66/clang/lib/Driver/ToolChains/Clang.cpp#L3949
inline wchar_t* as_writable_wcstr(char16_t* str) {
  return reinterpret_cast<wchar_t*>(str);
}

inline wchar_t* as_writable_wcstr(std::u16string& str) {
  return reinterpret_cast<wchar_t*>(data(str));
}

inline const wchar_t* as_wcstr(const char16_t* str) {
  return reinterpret_cast<const wchar_t*>(str);
}

inline const wchar_t* as_wcstr(std::u16string_view str) {
  return reinterpret_cast<const wchar_t*>(str.data());
}

// Utility functions to access the underlying string buffer as a char16_t
// pointer.
inline char16_t* as_writable_u16cstr(wchar_t* str) {
  return reinterpret_cast<char16_t*>(str);
}

inline char16_t* as_writable_u16cstr(std::wstring& str) {
  return reinterpret_cast<char16_t*>(data(str));
}

inline const char16_t* as_u16cstr(const wchar_t* str) {
  return reinterpret_cast<const char16_t*>(str);
}

inline const char16_t* as_u16cstr(std::wstring_view str) {
  return reinterpret_cast<const char16_t*>(str.data());
}

// Utility functions to convert between std::wstring_view and
// std::u16string_view.
inline std::wstring_view AsWStringView(std::u16string_view str) {
  return std::wstring_view(as_wcstr(str.data()), str.size());
}

inline std::u16string_view AsStringPiece16(std::wstring_view str) {
  return std::u16string_view(as_u16cstr(str.data()), str.size());
}

inline std::wstring AsWString(std::u16string_view str) {
  return std::wstring(as_wcstr(str.data()), str.size());
}

inline std::u16string AsString16(std::wstring_view str) {
  return std::u16string(as_u16cstr(str.data()), str.size());
}

// The following section contains overloads of the cross-platform APIs for
// std::wstring and std::wstring_view.
bool IsStringASCII(std::wstring_view str);

std::wstring ToLowerASCII(std::wstring_view str);

std::wstring ToUpperASCII(std::wstring_view str);

int CompareCaseInsensitiveASCII(std::wstring_view a,
                                std::wstring_view b);

inline bool EqualsCaseInsensitiveASCII(std::wstring_view a,
                                       std::wstring_view b) {
  return internal::EqualsCaseInsensitiveASCIIT(a, b);
}
inline bool EqualsCaseInsensitiveASCII(std::wstring_view a,
                                       std::string_view b) {
  return internal::EqualsCaseInsensitiveASCIIT(a, b);
}
inline bool EqualsCaseInsensitiveASCII(std::string_view a,
                                       std::wstring_view b) {
  return internal::EqualsCaseInsensitiveASCIIT(a, b);
}

bool RemoveChars(std::wstring_view input,
                 std::wstring_view remove_chars,
                 std::wstring* output);

bool ReplaceChars(std::wstring_view input,
                  std::wstring_view replace_chars,
                  std::wstring_view replace_with,
                  std::wstring* output);

bool TrimString(std::wstring_view input,
                std::wstring_view trim_chars,
                std::wstring* output);

std::wstring_view TrimString(std::wstring_view input,
                             std::wstring_view trim_chars,
                             TrimPositions positions);

TrimPositions TrimWhitespace(std::wstring_view input,
                             TrimPositions positions,
                             std::wstring* output);

std::wstring_view TrimWhitespace(std::wstring_view input,
                                 TrimPositions positions);

std::wstring CollapseWhitespace(
    std::wstring_view text,
    bool trim_sequences_with_line_breaks);

bool ContainsOnlyChars(std::wstring_view input,
                       std::wstring_view characters);

bool EqualsASCII(std::u16string_view str, std::string_view ascii);

bool StartsWith(
    std::wstring_view str,
    std::wstring_view search_for,
    CompareCase case_sensitivity = CompareCase::SENSITIVE);

bool EndsWith(
    std::wstring_view str,
    std::wstring_view search_for,
    CompareCase case_sensitivity = CompareCase::SENSITIVE);

void ReplaceFirstSubstringAfterOffset(
    std::wstring* str,
    size_t start_offset,
    std::wstring_view find_this,
    std::wstring_view replace_with);

void ReplaceSubstringsAfterOffset(std::wstring* str,
                                  size_t start_offset,
                                  std::wstring_view find_this,
                                  std::wstring_view replace_with);

wchar_t* WriteInto(std::wstring* str, size_t length_with_null);

std::wstring JoinString(span<const std::wstring> parts,
                        std::wstring_view separator);

std::wstring JoinString(span<const std::wstring_view> parts,
                        std::wstring_view separator);

std::wstring JoinString(
    std::initializer_list<std::wstring_view> parts,
    std::wstring_view separator);

std::wstring ReplaceStringPlaceholders(
    std::wstring_view format_string,
    base::span<const std::wstring> subst,
    std::vector<size_t>* offsets);

} // namespace bkfont::base
