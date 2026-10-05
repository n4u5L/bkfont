// Ported from: chromium/ui/gfx/linux/fontconfig_util.h
// Ported from: skia/src/ports/SkFontConfigInterface_direct.cpp

#pragma once

#include <fontconfig/fontconfig.h>
#include <memory>
#include <string>

namespace bkfont {

class StreamAsset;

// Older Fontconfig versions need a process-wide lock, including destruction
// of patterns and font sets. Declare this before their owning wrappers.
class FontconfigLocker {
public:
  FontconfigLocker();
  ~FontconfigLocker();
  FontconfigLocker(const FontconfigLocker&) = delete;
  FontconfigLocker& operator=(const FontconfigLocker&) = delete;
};

template <typename T, void (*Destroy)(T*)>
struct FontconfigDeleter {
  void operator()(T* value) const {
    Destroy(value);
  }
};
using FontconfigPattern = std::unique_ptr<FcPattern, FontconfigDeleter<FcPattern, FcPatternDestroy>>;
using FontconfigFontSet = std::unique_ptr<FcFontSet, FontconfigDeleter<FcFontSet, FcFontSetDestroy>>;
using FontconfigObjectSet = std::unique_ptr<FcObjectSet, FontconfigDeleter<FcObjectSet, FcObjectSetDestroy>>;

// Called under FontconfigLocker. The process keeps a reference to the config
// and disables background rescans, as gfx::GlobalFontConfig does.
FcConfig* GetGlobalFontConfig();
const char* GetFontconfigString(FcPattern* pattern, const char* property, int index = 0);
int GetFontconfigInteger(FcPattern* pattern, const char* property, int missing);
bool GetFontconfigBoolean(FcPattern* pattern, const char* property);
std::string GetFontconfigPath(FcPattern* pattern);
bool IsValidFallbackFont(FcPattern* pattern);
// Fontconfig paths are native POSIX bytes and need not be valid UTF-8.
std::unique_ptr<StreamAsset> OpenFontconfigStream(const std::string& filename);

} // namespace bkfont
