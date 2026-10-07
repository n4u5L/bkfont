// Ported from: skia/src/utils/win/SkDWrite.h

/*
 * Copyright 2012, 2014 Google Inc.
 * Copyright (c) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer. Redistributions in binary
 * form must reproduce the above copyright notice, this list of conditions and
 * the following disclaimer in the documentation and/or other materials provided
 * with the distribution. Neither the name of the copyright holder nor the names
 * of its contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Also from skia/src/utils/win/SkDWrite.cpp and DWriteFontTypeface::GetStyle
 * in skia/src/ports/SkTypeface_win_dw.cpp. COM RAII replaces SkTScopedComPtr.
 */
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef GetGlyphIndices
#undef GetGlyphIndices
#endif
#include <dwrite.h>
#include <dwrite_1.h>
#include <dwrite_2.h>
#include <dwrite_3.h>
#include <wrl/client.h>

#include <cstddef>
#include <memory>

#include "base/text/wtf_string.h"
#include "font_style.h"

namespace bkit {

using Microsoft::WRL::ComPtr;

// sk_cstring_to_wchar.
inline std::unique_ptr<wchar_t[]> ToWide(const String& value) {
  std::unique_ptr<wchar_t[]> result =
      std::make_unique<wchar_t[]>(value.length() + 1);
  for (std::size_t i = 0; i < value.length(); ++i) {
    result[i] = static_cast<wchar_t>(value.Is8Bit() ? value.Span8()[i]
                                                    : value.Span16()[i]);
  }
  result[value.length()] = L'\0';
  return result;
}

// sk_wchar_to_skstring.
inline String FromWide(const wchar_t* value, std::size_t length) {
  return String(base::span(reinterpret_cast<const char16_t*>(value), length));
}

// sk_get_locale_string. Returns a null String where upstream returns a
// failed HRESULT.
String DWriteLocalizedString(IDWriteLocalizedStrings* strings,
                             const wchar_t* locale = nullptr);

// DWriteFontTypeface::GetStyle.
FontStyle DWriteFontStyle(IDWriteFont* font, IDWriteFontFace* font_face);

} // namespace bkit
