// Ported from: skia/include/ports/SkFontMgr_FontConfigInterface.h

#pragma once

#include <memory>

#include "font_manager.h"

namespace bkfont {

// Chromium's default Linux manager (SkFontMgr_FCI with the direct Fontconfig
// interface). Matching uses Fontconfig; faces, metrics and rasterization use
// FreeType. Character fallback is supplied by font_fallback_linux.
std::shared_ptr<FontManager> MakeFontManagerFontconfig();

} // namespace bkfont
