// Ported from: skia/include/ports/SkTypeface_win.h

#pragma once

#include <memory>

#include "font_manager.h"

namespace bkit {

// SkFontMgr_New_DirectWrite() with the shared factory, the system collection
// and the system fallback. DirectWrite only enumerates, matches and falls
// back; the typefaces it returns rasterize through FreeType.
std::shared_ptr<FontManager> MakeFontManagerDirectWrite();

} // namespace bkit
