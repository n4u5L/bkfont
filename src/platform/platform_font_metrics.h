// Ported from: skia/include/core/SkFontMetrics.h

#pragma once

#include <cstdint>

namespace bkfont {

// SkFontMetrics. The Metrics for a single font, in the typeface's y-down
// coordinates.
struct PlatformFontMetrics {
  // FontMetricsFlags indicate when certain metrics are valid; the underline
  // or strikeout metrics may be valid and zero. Fonts with embedded bitmaps
  // may not have valid underline or strikeout metrics.
  enum FontMetricsFlags {
    kUnderlineThicknessIsValid_Flag = 1 << 0,
    kUnderlinePositionIsValid_Flag = 1 << 1,
    kStrikeoutThicknessIsValid_Flag = 1 << 2,
    kStrikeoutPositionIsValid_Flag = 1 << 3,
    kBoundsInvalid_Flag = 1 << 4,
  };

  // FontMetricsFlags indicating which metrics are valid.
  std::uint32_t flags = 0;
  // greatest extent above origin of any glyph bounding box, typically negative
  float top = 0;
  // distance to reserve above baseline, typically negative
  float ascent = 0;
  // distance to reserve below baseline, typically positive
  float descent = 0;
  // greatest extent below origin of any glyph bounding box, typically positive
  float bottom = 0;
  // distance to add between lines, typically positive or zero
  float leading = 0;
  // average character width, zero if unknown
  float avg_char_width = 0;
  // maximum character width, zero if unknown
  float max_char_width = 0;
  // greatest extent to left of origin of any glyph bounding box, typically
  // negative; deprecated with variable fonts
  float x_min = 0;
  // greatest extent to right of origin of any glyph bounding box, typically
  // positive; deprecated with variable fonts
  float x_max = 0;
  // height of lower-case 'x', zero if unknown, typically negative
  float x_height = 0;
  // height of an upper-case letter, zero if unknown, typically negative
  float cap_height = 0;
  // underline thickness
  float underline_thickness = 0;
  // distance from baseline to top of stroke, typically positive
  float underline_position = 0;
  // strikeout thickness
  float strikeout_thickness = 0;
  // distance from baseline to bottom of stroke, typically negative
  float strikeout_position = 0;

  // Returns true if FontMetrics has a valid underline thickness, and sets
  // thickness to that value. If the underline thickness is not valid, return
  // false, and ignore thickness.
  bool HasUnderlineThickness(float* thickness) const {
    if (flags & kUnderlineThicknessIsValid_Flag) {
      *thickness = underline_thickness;
      return true;
    }
    return false;
  }

  // Returns true if FontMetrics has a valid underline position, and sets
  // position to that value. If the underline position is not valid, return
  // false, and ignore position.
  bool HasUnderlinePosition(float* position) const {
    if (flags & kUnderlinePositionIsValid_Flag) {
      *position = underline_position;
      return true;
    }
    return false;
  }

  // Returns true if FontMetrics has a valid strikeout thickness, and sets
  // thickness to that value. If the underline thickness is not valid, return
  // false, and ignore thickness.
  bool HasStrikeoutThickness(float* thickness) const {
    if (flags & kStrikeoutThicknessIsValid_Flag) {
      *thickness = strikeout_thickness;
      return true;
    }
    return false;
  }

  // Returns true if FontMetrics has a valid strikeout position, and sets
  // position to that value. If the underline position is not valid, return
  // false, and ignore position.
  bool HasStrikeoutPosition(float* position) const {
    if (flags & kStrikeoutPositionIsValid_Flag) {
      *position = strikeout_position;
      return true;
    }
    return false;
  }

  // Returns true if FontMetrics has a valid fXMin, fXMax, fTop, fBottom.
  bool HasBounds() const {
    return !(flags & kBoundsInvalid_Flag);
  }
};

} // namespace bkfont
