// Ported from: blink/renderer/platform/fonts/win/font_unique_name_lookup_win.h

#pragma once

#include <cstdint>
#include <memory>

#include "base/text/wtf_string.h"
#include "platform/typeface.h"

namespace bkit {

// FontUniqueNameLookupWin. The browser-side DWriteFontProxyImpl::MatchUniqueFont
// runs in process instead of over mojo.
class FontUniqueNameLookupWin final {
public:
  static std::shared_ptr<Typeface> MatchUniqueName(const String& font_unique_name);

private:
  static std::shared_ptr<Typeface> MatchUniqueNameSingleLookup(const String& font_unique_name);
  static std::shared_ptr<Typeface> InstantiateFromFileAndTtcIndex(const String& file_path, std::uint32_t ttc_index);
};

} // namespace bkit
