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
 * Source: third_party/skia/src/ports/SkTypeface_win_dw.cpp,
 * SkFontMgr_win_dw.cpp, SkScalerContext_win_dw.cpp and
 * third_party/skia/src/utils/win/SkDWriteFontFileStream.cpp.
 * This port replaces Skia ownership/containers with COM RAII, shared ownership,
 * and the port's WTF containers.
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

#include <algorithm>
#include <atomic>
#include <cstring>
#include <cwchar>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>

#include "font_face.h"
#include "font_manager.h"

namespace bkfont {
using Microsoft::WRL::ComPtr;

inline std::uint32_t SwapFontTag(std::uint32_t tag) {
  return ((tag & 0xffu) << 24) | ((tag & 0xff00u) << 8) | ((tag & 0xff0000u) >> 8) | (tag >> 24);
}

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

inline String FromWide(const wchar_t* value, std::size_t length) {
  return String(base::span(reinterpret_cast<const char16_t*>(value), length));
}

String DWriteLocalizedString(IDWriteLocalizedStrings* strings,
                             const wchar_t* locale = nullptr);
FontStyle DWriteStyle(IDWriteFont* font, IDWriteFontFace* face);
Vector<LocalizedFontName> FamilyNamesFromNameTable(
    std::span<const std::uint8_t> table);

class FontCollectionLoaders;

// SkScalerContext_win_dw.cpp::maybe_dw_mutex protects a process-wide DWrite
// implementation on Windows 8/8.1. Face4 makes that protection unnecessary.
// A recursive exclusive lock replaces the old shared/exclusive helper so nested
// font-table/metric adapter calls retain the same protection without deadlock.
class DWriteMutexLock final {
public:
  explicit DWriteMutexLock(IDWriteFontFace* face) {
    ComPtr<IDWriteFontFace4> face4;
    if (FAILED(face->QueryInterface(IID_PPV_ARGS(&face4)))) {
      mutex_ = &GlobalMutex();
      mutex_->lock();
    }
  }
  ~DWriteMutexLock() {
    if (mutex_)
      mutex_->unlock();
  }
  DWriteMutexLock(const DWriteMutexLock&) = delete;
  DWriteMutexLock& operator=(const DWriteMutexLock&) = delete;

private:
  static std::recursive_mutex& GlobalMutex() {
    static std::recursive_mutex mutex;
    return mutex;
  }
  std::recursive_mutex* mutex_ = nullptr;
};

struct FontFace::Impl {
  ComPtr<IDWriteFactory> factory;
  // Declared before the face so registration outlives all COM face references.
  std::shared_ptr<FontCollectionLoaders> loaders;
  ComPtr<IDWriteFontFace> face;
  ComPtr<IDWriteFont> font;
  ComPtr<IDWriteFontFamily> family;
  std::uint16_t palette_index = 0;
  std::unique_ptr<PlatformPaletteOverride[]> palette_overrides;
  std::size_t palette_override_count = 0;
  std::unique_ptr<std::uint32_t[]> palette_colors;
  std::size_t palette_color_count = 0;
  std::uint32_t unique_id = 0;
  void InitializePalette();
};

} // namespace bkfont
