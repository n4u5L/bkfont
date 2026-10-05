// Ported from: blink/renderer/platform/fonts/font_family_names.json5
// Initialization mirrors Blink generated names; call after bkfont::Initialize.
#include "font_family_names.h"
#include "base/static_constructors.h"
#include <new>
namespace bkfont::font_family_names {

DEFINE_GLOBAL(, AtomicString, kWebkitStandard);
DEFINE_GLOBAL(, AtomicString, kSystemUi);
DEFINE_GLOBAL(, AtomicString, kArial);
DEFINE_GLOBAL(, AtomicString, kCalibri);
DEFINE_GLOBAL(, AtomicString, kCourierNew);
DEFINE_GLOBAL(, AtomicString, kCourier);
DEFINE_GLOBAL(, AtomicString, kHelvetica);
DEFINE_GLOBAL(, AtomicString, kHelveticaNeue);
DEFINE_GLOBAL(, AtomicString, kLucidaGrande);
DEFINE_GLOBAL(, AtomicString, kMicrosoftSansSerif);
DEFINE_GLOBAL(, AtomicString, kMSSansSerif);
DEFINE_GLOBAL(, AtomicString, kMSSerif);
DEFINE_GLOBAL(, AtomicString, kMSUIGothic);
DEFINE_GLOBAL(, AtomicString, kRoboto);
DEFINE_GLOBAL(, AtomicString, kSans);
DEFINE_GLOBAL(, AtomicString, kSegoeUI);
DEFINE_GLOBAL(, AtomicString, kTimesNewRoman);
DEFINE_GLOBAL(, AtomicString, kTimes);
DEFINE_GLOBAL(, AtomicString, kCursive);
DEFINE_GLOBAL(, AtomicString, kFantasy);
DEFINE_GLOBAL(, AtomicString, kMonospace);
DEFINE_GLOBAL(, AtomicString, kSansSerif);
DEFINE_GLOBAL(, AtomicString, kSerif);
DEFINE_GLOBAL(, AtomicString, kMath);
DEFINE_GLOBAL(, AtomicString, kBlinkMacSystemFont);
void Init() {
  new (static_cast<void*>(kWebkitStandardStorage)) AtomicString("-webkit-standard");
  new (static_cast<void*>(kSystemUiStorage)) AtomicString("system-ui");
  new (static_cast<void*>(kArialStorage)) AtomicString("Arial");
  new (static_cast<void*>(kCalibriStorage)) AtomicString("Calibri");
  new (static_cast<void*>(kCourierNewStorage)) AtomicString("Courier New");
  new (static_cast<void*>(kCourierStorage)) AtomicString("Courier");
  new (static_cast<void*>(kHelveticaStorage)) AtomicString("Helvetica");
  new (static_cast<void*>(kHelveticaNeueStorage)) AtomicString("Helvetica Neue");
  new (static_cast<void*>(kLucidaGrandeStorage)) AtomicString("Lucida Grande");
  new (static_cast<void*>(kMicrosoftSansSerifStorage)) AtomicString("Microsoft Sans Serif");
  new (static_cast<void*>(kMSSansSerifStorage)) AtomicString("MS Sans Serif");
  new (static_cast<void*>(kMSSerifStorage)) AtomicString("MS Serif");
  new (static_cast<void*>(kMSUIGothicStorage)) AtomicString("MS UI Gothic");
  new (static_cast<void*>(kRobotoStorage)) AtomicString("Roboto");
  new (static_cast<void*>(kSansStorage)) AtomicString("Sans");
  new (static_cast<void*>(kSegoeUIStorage)) AtomicString("Segoe UI");
  new (static_cast<void*>(kTimesNewRomanStorage)) AtomicString("Times New Roman");
  new (static_cast<void*>(kTimesStorage)) AtomicString("Times");
  new (static_cast<void*>(kCursiveStorage)) AtomicString("cursive");
  new (static_cast<void*>(kFantasyStorage)) AtomicString("fantasy");
  new (static_cast<void*>(kMonospaceStorage)) AtomicString("monospace");
  new (static_cast<void*>(kSansSerifStorage)) AtomicString("sans-serif");
  new (static_cast<void*>(kSerifStorage)) AtomicString("serif");
  new (static_cast<void*>(kMathStorage)) AtomicString("math");
  new (static_cast<void*>(kBlinkMacSystemFontStorage)) AtomicString("BlinkMacSystemFont");
}

} // namespace bkfont::font_family_names
