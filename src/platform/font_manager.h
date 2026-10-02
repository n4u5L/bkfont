/*
 * Copyright 2014 Google Inc.
 * Adapted from third_party/skia/src/ports/SkFontMgr_win_dw.cpp.
 * BSD license: src/platform/dwrite_internal.h.
 */
#pragma once

#include "font_face.h"

namespace blink {

class FontManager final {
public:
  static std::shared_ptr<FontManager> Create();
  ~FontManager();
  FontManager(const FontManager&) = delete;
  FontManager& operator=(const FontManager&) = delete;

  std::uint32_t FamilyCount() const;
  String FamilyName(std::uint32_t index) const;
  Vector<FontStyle> FamilyStyles(const String& family) const;
  std::shared_ptr<FontFace> MatchFamily(const String& family,
                                        const FontStyle& style) const;
  std::shared_ptr<FontFace> MatchUniqueName(const String& unique_name) const;
  std::shared_ptr<FontFace> MatchCharacter(const String& family,
                                           const FontStyle& style,
                                           const String& locale,
                                           std::uint32_t codepoint) const;
  // Matches onLegacyMakeTypeface, including the space-character fallback.
  std::shared_ptr<FontFace> DefaultFont(const FontStyle& style) const;
  std::shared_ptr<FontFace> CreateFromFile(const String& path,
                                           std::uint32_t collection_index = 0) const;
  std::shared_ptr<FontFace> CreateFromData(
      std::span<const std::uint8_t> data,
      std::uint32_t collection_index = 0) const;
  void PurgeUnusedFaces() const;

private:
  struct Impl;
  explicit FontManager(std::unique_ptr<Impl> implementation);
  std::unique_ptr<Impl> impl_;
};

} // namespace blink
