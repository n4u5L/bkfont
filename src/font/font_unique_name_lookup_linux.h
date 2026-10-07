// Ported from: blink/renderer/platform/fonts/linux/font_unique_name_lookup_linux.h

#pragma once

#include <memory>

#include "base/text/wtf_string.h"
#include "platform/typeface.h"

namespace bkit {

class FontUniqueNameLookupLinux {
public:
  static std::shared_ptr<Typeface> MatchUniqueName(const String& font_unique_name);
};

} // namespace bkit
