// Ported from: blink/renderer/platform/fonts/linux/font_unique_name_lookup_linux.cc
// Ported from: chromium/components/services/font/fontconfig_matching.cc

// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "font_unique_name_lookup_linux.h"

#include <cerrno>
#include <fcntl.h>
#include <optional>
#include <string>
#include <unistd.h>
#include <unicode/utf8.h>

#include "platform/font_manager_fontconfig.h"
#include "platform/fontconfig_util.h"

namespace bkfont {

namespace {

struct FontConfigMatchResult {
  std::string filename;
  int ttc_index;
};

std::optional<FontConfigMatchResult> FindFontBySpecifiedName(const char* property, const std::string& name) {
  // base::IsStringUTF8 rejects unpaired surrogates and Unicode noncharacters.
  for (std::size_t i = 0; i < name.size();) {
    UChar32 character;
    U8_NEXT(name.data(), i, name.size(), character);
    if (!U_IS_UNICODE_CHAR(character)) return std::nullopt;
  }
  FontconfigPattern pattern(FcPatternCreate());
  FcPatternAddString(pattern.get(), property, reinterpret_cast<const FcChar8*>(name.c_str()));
  FcPatternAddBool(pattern.get(), FC_SCALABLE, FcTrue);
  FontconfigObjectSet objects(FcObjectSetCreate());
  FcObjectSetAdd(objects.get(), FC_FILE);
  FcObjectSetAdd(objects.get(), FC_INDEX);
  FontconfigFontSet fonts(FcFontList(GetGlobalFontConfig(), pattern.get(), objects.get()));
  if (!fonts || !fonts->nfont) return std::nullopt;

  FcPattern* current = fonts->fonts[0];
  const char* path = GetFontconfigString(current, FC_FILE);
  if (!path) return std::nullopt;
  const char* sysroot = reinterpret_cast<const char*>(FcConfigGetSysRoot(nullptr));
  const std::string filename = std::string(sysroot ? sysroot : "") + path;

  // Preserve the upstream case-sensitive extension allowlist, including its
  // distinct treatment of upper-case collection extensions.
  bool is_sfnt = false;
  for (const char* extension : {".ttf", ".otc", ".TTF", ".ttc", ".otf", ".OTF"}) {
    if (filename.ends_with(extension)) {
      is_sfnt = true;
      break;
    }
  }
  if (!is_sfnt) return std::nullopt;
  int file;
  do {
    file = open(filename.c_str(), O_RDONLY | O_CLOEXEC);
  } while (file < 0 && errno == EINTR);
  if (file < 0) return std::nullopt;
  close(file);
  const int ttc_index = GetFontconfigInteger(current, FC_INDEX, 0);
  if (ttc_index < 0) return std::nullopt;
  return FontConfigMatchResult{filename, ttc_index};
}

} // namespace

std::shared_ptr<Typeface> FontUniqueNameLookupLinux::MatchUniqueName(const String& font_unique_name) {
  const std::string name = font_unique_name.Utf8();
  std::optional<FontConfigMatchResult> match;
  {
    FontconfigLocker lock;
    match = FindFontBySpecifiedName(FC_POSTSCRIPT_NAME, name);
    if (!match) match = FindFontBySpecifiedName(FC_FULLNAME, name);
  }
  if (!match) return nullptr;
  return MakeFontManagerFontconfig()->MakeFromStream(OpenFontconfigStream(match->filename), match->ttc_index);
}

} // namespace bkfont
