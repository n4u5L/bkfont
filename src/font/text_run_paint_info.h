// Ported from: blink/renderer/platform/fonts/text_run_paint_info.h

#pragma once

#include "text/text_run.h"

namespace bkit {

// Container for parameters needed to paint TextRun.
struct TextRunPaintInfo {
public:
  explicit TextRunPaintInfo(const TextRun& r)
      : run(r), from(0), to(r.length()) {
  }

  const TextRun& run;
  unsigned from;
  unsigned to;
};

} // namespace bkit
