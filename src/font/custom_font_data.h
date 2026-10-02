// Port source: third_party/blink/renderer/platform/fonts/custom_font_data.h
/*
 * Copyright (C) 2013 Google, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#pragma once

#include <memory>

namespace blink {

// The `CustomFontData` provides an interface of loadable font resource and
// lifetime management. `SimpleFontData` owns its instance.
//
// Following classes construct an instance:
//  * `BinaryDataFontFaceSource` as loaded font resource
//  * `LocalFontFaceSource` as derived class `CSSCustomFontData`
//  * `RemoteFontFaceSource` as derived class `CSSCustomFontData`
class CustomFontData {
public:
  CustomFontData() = default;
  virtual ~CustomFontData() = default;

  virtual void BeginLoadIfNeeded() const {
  }
  virtual bool IsLoading() const {
    return false;
  }
  virtual bool IsLoadingFallback() const {
    return false;
  }
  virtual bool ShouldSkipDrawing() const {
    return false;
  }
  virtual bool IsPendingDataUrl() const {
    return false;
  }
};

} // namespace blink
