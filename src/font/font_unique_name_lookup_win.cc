// Ported from: blink/renderer/platform/fonts/win/font_unique_name_lookup_win.cc

// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "font_unique_name_lookup_win.h"

#include <utility>

#include "font_cache.h"
#include "platform/data.h"
#include "platform/dwrite_internal.h"

namespace bkfont {

namespace {

// FontFilePathAndTtcIndex from content/browser/renderer_host/
// dwrite_font_file_util_win.cc.
HRESULT FontFilePathAndTtcIndex(IDWriteFontFace* font_face,
                                String& file_path,
                                std::uint32_t& ttc_index) {
  UINT32 file_count;
  HRESULT hr;
  hr = font_face->GetFiles(&file_count, nullptr);
  if (FAILED(hr)) {
    return hr;
  }

  // We've learned from the DirectWrite team at MS that the number of font
  // files retrieved per IDWriteFontFile can only ever be 1. Other font formats
  // such as Type 1, which represent one font in multiple files, are currently
  // not supported in the API (as of December 2018, Windows 10). In Chrome we
  // do not plan to support Type 1 fonts, or generally other font formats
  // different from OpenType, hence no need to loop over file_count or retrieve
  // multiple files.
  if (file_count > 1) {
    return E_FAIL;
  }

  ComPtr<IDWriteFontFile> font_file;
  hr = font_face->GetFiles(&file_count, &font_file);
  if (FAILED(hr)) {
    return hr;
  }

  ComPtr<IDWriteFontFileLoader> loader;
  hr = font_file->GetLoader(&loader);
  if (FAILED(hr)) {
    return hr;
  }

  ComPtr<IDWriteLocalFontFileLoader> local_loader;
  hr = loader.As(&local_loader);
  if (FAILED(hr)) {
    return hr;
  }

  const void* key;
  UINT32 key_size;
  hr = font_file->GetReferenceKey(&key, &key_size);
  if (FAILED(hr)) {
    return hr;
  }

  UINT32 path_length = 0;
  hr = local_loader->GetFilePathLengthFromKey(key, key_size, &path_length);
  if (FAILED(hr)) {
    return hr;
  }
  std::unique_ptr<wchar_t[]> path = std::make_unique<wchar_t[]>(static_cast<std::size_t>(path_length) + 1);
  hr = local_loader->GetFilePathFromKey(key, key_size, path.get(), path_length + 1);
  if (FAILED(hr)) {
    return hr;
  }
  file_path = FromWide(path.get(), path_length);
  ttc_index = font_face->GetIndex();
  return S_OK;
}

// DWriteFontProxyImpl::MatchUniqueFont from content/browser/renderer_host/
// dwrite_font_proxy_impl_win.cc. Returns false where the callback runs with
// an invalid file.
bool MatchUniqueFont(const String& unique_font_name, String* file_path, std::uint32_t* ttc_index) {
  ComPtr<IDWriteFactory> factory;
  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(factory.GetAddressOf())))) {
    return false;
  }
  ComPtr<IDWriteFontCollection> collection;
  if (FAILED(factory->GetSystemFontCollection(&collection, FALSE))) {
    return false;
  }

  ComPtr<IDWriteFontCollection1> collection1;
  HRESULT hr = collection.As(&collection1);
  if (FAILED(hr)) {
    return false;
  }
  // In non-testing cases this is identical to factory3_->GetSystemFontSet().
  ComPtr<IDWriteFontSet> system_font_set;
  hr = collection1->GetFontSet(&system_font_set);
  if (FAILED(hr)) {
    return false;
  }

  ComPtr<IDWriteFontSet> filtered_set;

  const std::unique_ptr<wchar_t[]> unique_font_name_wide = ToWide(unique_font_name);
  auto filter_set = [&system_font_set, &filtered_set,
                     &unique_font_name_wide](DWRITE_FONT_PROPERTY_ID property_id) {
    DWRITE_FONT_PROPERTY search_property = {property_id,
                                            unique_font_name_wide.get(), L""};
    // GetMatchingFonts() matches all languages according to:
    // https://docs.microsoft.com/en-us/windows/desktop/api/dwrite_3/ns-dwrite_3-dwrite_font_property
    HRESULT hr =
        system_font_set->GetMatchingFonts(&search_property, 1, &filtered_set);
    return SUCCEEDED(hr);
  };

  // Search PostScript name first, otherwise try searching for full font name.
  // Return if filtering failed.
  if (!filter_set(DWRITE_FONT_PROPERTY_ID_POSTSCRIPT_NAME)) {
    return false;
  }

  if (!filtered_set->GetFontCount() &&
      !filter_set(DWRITE_FONT_PROPERTY_ID_FULL_NAME)) {
    return false;
  }

  if (!filtered_set->GetFontCount()) {
    return false;
  }

  ComPtr<IDWriteFontFaceReference> first_font;
  hr = filtered_set->GetFontFaceReference(0, &first_font);
  if (FAILED(hr)) {
    return false;
  }

  ComPtr<IDWriteFontFace3> first_font_face_3;
  hr = first_font->CreateFontFace(&first_font_face_3);
  if (FAILED(hr)) {
    return false;
  }

  ComPtr<IDWriteFontFace> first_font_face;
  hr = first_font_face_3.As<IDWriteFontFace>(&first_font_face);
  if (FAILED(hr)) {
    return false;
  }

  if (FAILED(FontFilePathAndTtcIndex(first_font_face.Get(), *file_path, *ttc_index))) {
    return false;
  }
  return true;
}

} // namespace

std::shared_ptr<Typeface> FontUniqueNameLookupWin::MatchUniqueName(const String& font_unique_name) {
  return MatchUniqueNameSingleLookup(font_unique_name);
}

std::shared_ptr<Typeface> FontUniqueNameLookupWin::MatchUniqueNameSingleLookup(const String& font_unique_name) {
  String font_file;
  std::uint32_t ttc_index = 0;

  if (!MatchUniqueFont(font_unique_name, &font_file, &ttc_index)) {
    return nullptr;
  }

  return InstantiateFromFileAndTtcIndex(font_file, ttc_index);
}

// Used for font matching with single lookup case only.
std::shared_ptr<Typeface> FontUniqueNameLookupWin::InstantiateFromFileAndTtcIndex(const String& file_path,
                                                                                  std::uint32_t ttc_index) {
  std::shared_ptr<Data> data = Data::MakeFromFileName(file_path);
  if (!data) {
    return nullptr;
  }
  // skia::DefaultFontMgr() is the DirectWrite manager on Windows.
  std::shared_ptr<FontManager> mgr = FontCache::Get().GetFontManager();
  return mgr->MakeFromData(std::move(data), static_cast<int>(ttc_index));
}

} // namespace bkfont
