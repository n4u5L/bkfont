// Ported from: chromium/ui/gfx/font_fallback_linux.h

#pragma once

#include <cstdint>
#include <string>

namespace bkfont {

struct FallbackFontData {
  std::string name;
  std::string filepath;
  int fontconfig_interface_id = 0;
  int ttc_index = 0;
  bool is_bold = false;
  bool is_italic = false;
};

bool GetFallbackFontForChar(std::int32_t character, const std::string& locale, FallbackFontData* fallback_font);

} // namespace bkfont
