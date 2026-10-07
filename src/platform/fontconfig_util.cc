// Ported from: chromium/ui/gfx/linux/fontconfig_util.cc
// Ported from: chromium/ui/gfx/font_fallback_linux.cc
// Ported from: skia/src/ports/SkFontConfigInterface_direct.cpp
// Ported from: skia/src/core/SkStream.cpp

// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "fontconfig_util.h"

#include <cstring>
#include <fstream>
#include <unistd.h>
#include <utility>

#include "base/mutex.h"
#include "stream.h"

namespace bkit {

namespace {

Mutex& FontconfigMutex() {
  static Mutex& mutex = *new Mutex;
  return mutex;
}

} // namespace

FontconfigLocker::FontconfigLocker() {
  if (FcGetVersion() < 21393) FontconfigMutex().Acquire();
}

FontconfigLocker::~FontconfigLocker() {
  if (FcGetVersion() < 21393) FontconfigMutex().Release();
}

FcConfig* GetGlobalFontConfig() {
  static FcConfig* config = [] {
    FcInit();
    FcConfig* current = FcConfigGetCurrent();
    FcConfigReference(current);
    FcConfigSetRescanInterval(current, 0);
    return current;
  }();
  return config;
}

const char* GetFontconfigString(FcPattern* pattern, const char* property, int index) {
  FcChar8* value;
  return FcPatternGetString(pattern, property, index, &value) == FcResultMatch
             ? reinterpret_cast<const char*>(value)
             : nullptr;
}

int GetFontconfigInteger(FcPattern* pattern, const char* property, int missing) {
  int value;
  return FcPatternGetInteger(pattern, property, 0, &value) == FcResultMatch ? value : missing;
}

bool GetFontconfigBoolean(FcPattern* pattern, const char* property) {
  FcBool value = FcFalse;
  return FcPatternGetBool(pattern, property, 0, &value) == FcResultMatch && value != FcFalse;
}

std::string GetFontconfigPath(FcPattern* pattern) {
  const char* path = GetFontconfigString(pattern, FC_FILE);
  std::string filename = path ? path : "";
  const char* sysroot = reinterpret_cast<const char*>(FcConfigGetSysRoot(nullptr));
  if (!sysroot) return filename;
  if (!filename.empty() && filename[0] == '/') filename.erase(0, 1);
  if (filename.empty()) return {};
  std::string root(sysroot);
  if (!root.empty() && root.back() != '/') root += '/';
  return root + filename;
}

bool IsValidFallbackFont(FcPattern* pattern) {
  if (!GetFontconfigBoolean(pattern, FC_SCALABLE)) return false;
  const char* format = GetFontconfigString(pattern, FC_FONTFORMAT);
  if (!format || (std::strcmp(format, "TrueType") != 0 && std::strcmp(format, "CFF") != 0)) return false;
  const std::string path = GetFontconfigPath(pattern);
  return !path.empty() && access(path.c_str(), R_OK) == 0;
}

std::unique_ptr<StreamAsset> OpenFontconfigStream(const std::string& filename) {
  // Like Stream::MakeFromFile, this port reads the file into a MemoryStream.
  // Keep native filenames intact instead of passing through WTF's UTF-8 decoder.
  std::ifstream file(filename, std::ios::binary);
  if (!file) return nullptr;
  file.seekg(0, std::ios::end);
  const std::streamoff length = file.tellg();
  if (length < 0) return nullptr;
  file.seekg(0, std::ios::beg);
  auto data = Data::MakeUninitialized(static_cast<std::size_t>(length));
  if (length && !file.read(static_cast<char*>(data->writable_data()), length)) return nullptr;
  return MemoryStream::Make(std::move(data));
}

} // namespace bkit
