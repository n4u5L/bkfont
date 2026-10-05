// Ported from: blink/renderer/platform/fonts/font_metrics.cc
// (Linux branch)
/*
 * Copyright (C) 2005, 2008, 2010 Apple Inc. All rights reserved.
 * Copyright (C) 2006 Alexey Proskuryakov
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "font_metrics.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "base/allocator/partitions.h"
#include "base/notreached.h"
#include "font_platform_data.h"
#include "platform/platform_font.h"
#include "platform/platform_font_metrics.h"
#include "platform/typeface.h"
#include "vdmx_parser.h"

namespace bkfont {

// This is the largest VDMX table which we'll try to load and parse.
static const std::size_t kMaxVDMXTableSize = 1024 * 1024; // 1 MB

void FontMetrics::AscentDescentWithHacks(
    float& ascent,
    float& descent,
    const FontPlatformData& platform_data,
    const PlatformFont& font,
    bool subpixel_ascent_descent,
    std::optional<float> ascent_override,
    std::optional<float> descent_override) {
  Typeface* face = font.GetTypeface().get();

  PlatformFontMetrics metrics;
  font.GetMetrics(&metrics);

  if (ascent_override) {
    metrics.ascent = -platform_data.size() * ascent_override.value();
  }
  if (descent_override) {
    metrics.descent = platform_data.size() * descent_override.value();
  }

  int vdmx_ascent = 0, vdmx_descent = 0;
  bool is_vdmx_valid = false;

  // Manually digging up VDMX metrics is only applicable when bytecode hinting
  // using FreeType. All platforms in this port use this Linux metrics path.
  static const std::uint32_t kVdmxTag = 0x56444d58; // 'VDMX'
  int pixel_size = platform_data.size() + 0.5;
  // TODO(xiaochengh): How do we support ascent/descent override with VDMX?
  if (!ascent_override && !descent_override && !font.IsForceAutoHinting() &&
      (font.GetHinting() == FontHinting::kFull ||
       font.GetHinting() == FontHinting::kNormal)) {
    std::size_t vdmx_size = face->GetTableSize(kVdmxTag);
    if (vdmx_size && vdmx_size < kMaxVDMXTableSize) {
      auto* vdmx_table = static_cast<std::uint8_t*>(
          Partitions::FastMalloc(vdmx_size, "FontMetrics"));
      if (vdmx_table &&
          face->GetTableData(kVdmxTag, 0, vdmx_size, vdmx_table) == vdmx_size &&
          ParseVDMX(&vdmx_ascent, &vdmx_descent, vdmx_table, vdmx_size,
                    pixel_size)) {
        is_vdmx_valid = true;
      }
      Partitions::FastFree(vdmx_table);
    }
  }

  // Match the upstream Linux rounding and tiny-font exceptions in this order.
  if (is_vdmx_valid) {
    ascent = vdmx_ascent;
    descent = -vdmx_descent;
  } else if (subpixel_ascent_descent &&
             (-metrics.ascent < 3 || -metrics.ascent + metrics.descent < 2)) {
    // Rounding tiny fonts can make different text baselines coincide.
    ascent = -metrics.ascent;
    descent = metrics.descent;
  } else {
    // SkScalarRoundToScalar does the addition and floor in double before
    // converting back to float, including for negative inputs.
    ascent = static_cast<float>(std::floor(static_cast<double>(-metrics.ascent) + 0.5));
    descent = static_cast<float>(std::floor(static_cast<double>(metrics.descent) + 0.5));

    // Avoid clipping descenders in overflow:hidden containers with subpixel
    // positioning. Borrow one unit from the ascent when possible.
    if (descent < metrics.descent &&
        platform_data.GetFontRenderStyle().use_subpixel_positioning &&
        ascent >= 1) {
      ++descent;
      --ascent;
    }
  }
}

float FontMetrics::FloatAscentInternal(
    FontBaseline baseline_type,
    ApplyBaselineTable apply_baseline_table) const {
  switch (baseline_type) {
  case kAlphabeticBaseline:
    NOTREACHED();
  case kCentralBaseline:
    return FloatHeight() / 2;

    // The following computations are based on 'dominant-baseline' support in
    // the legacy SVG <text>.

  case kTextUnderBaseline:
    return FloatHeight();
  case kIdeographicUnderBaseline:
    if (ideographic_baseline_position_.has_value() && apply_baseline_table) {
      return float_ascent_ - ideographic_baseline_position_.value();
    }
    return FloatHeight();
  case kXMiddleBaseline:
    return float_ascent_ - XHeight() / 2;
  case kMathBaseline:
    // TODO(layout-dev): Should refer to 'math' in OpenType or 'bsln' value 4
    // in TrueType AAT.
    return float_ascent_ * 0.5f;
  case kHangingBaseline:
    // Upstream uses the comma operator here, so only `apply_baseline_table`
    // decides the branch. Kept as-is; the cast only silences C4834.
    if (static_cast<void>(hanging_baseline_position_.has_value()),
        apply_baseline_table) {
      return float_ascent_ - hanging_baseline_position_.value();
    }
    return float_ascent_ * 0.2f;
  case kTextOverBaseline:
    return 0;
  }

  NOTREACHED();
}

int FontMetrics::IntAscentInternal(
    FontBaseline baseline_type,
    ApplyBaselineTable apply_baseline_table) const {
  switch (baseline_type) {
  case kAlphabeticBaseline:
    NOTREACHED();
  case kCentralBaseline:
    return Height() - Height() / 2;

    // The following computations are based on 'dominant-baseline' support in
    // the legacy SVG <text>.

  case kTextUnderBaseline:
    return Height();
  case kIdeographicUnderBaseline:
    if (ideographic_baseline_position_.has_value() && apply_baseline_table) {
      return static_cast<int>(
          int_ascent_ - static_cast<int>(lroundf(ideographic_baseline_position_.value())));
    }
    return Height();
  case kXMiddleBaseline:
    return int_ascent_ - static_cast<int>(XHeight() / 2);
  case kMathBaseline:
    if (hanging_baseline_position_.has_value() && apply_baseline_table) {
      return int_ascent_ - static_cast<int>(lroundf(hanging_baseline_position_.value()));
    }
    return int_ascent_ / 2;
  case kHangingBaseline:
    // TODO(layout-dev): Should refer to 'hang' in OpenType or 'bsln' value 3
    // in TrueType AAT.
    return int_ascent_ * 2 / 10;
  case kTextOverBaseline:
    return 0;
  }

  NOTREACHED();
}

} // namespace bkfont
