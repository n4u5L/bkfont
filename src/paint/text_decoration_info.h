// Ported from: blink/renderer/core/paint/text_decoration_info.h
// Ported from: blink/renderer/core/layout/text_decoration_offset.h
#pragma once

#include "decoration_line_painter.h"
#include "style/computed_style.h"

namespace bkit {
class SimpleFontData;
enum class ResolvedUnderlinePosition { kAuto, kFromFont, kUnder, kOver };
struct DecoratingBox {
  const ComputedStyle* style;
  float content_top;
};

// Non-SVG decoration geometry in the line-relative coordinate system.
class TextDecorationInfo {
public:
  TextDecorationInfo(ScalarPoint origin, float width, const ComputedStyle&, const Font&,
                     const AppliedTextDecoration&, const DecoratingBox* = nullptr);
  bool HasUnderline() const { return underline_; }
  bool HasOverline() const { return overline_; }
  bool HasLineThrough() const;
  bool HasError() const;
  DecorationGeometry Underline() const;
  DecorationGeometry Overline() const;
  DecorationGeometry LineThrough() const;
  DecorationGeometry ErrorLine() const;
  Color4f Color() const;
  float Baseline() const;

private:
  float UnderOffset(const Length&, FontVerticalPositionType, float computed_size) const;
  float UnderlineOffset(const Length&) const;
  DecorationGeometry MakeLine(TextDecorationLine, float offset) const;
  const ComputedStyle& style_;
  const ComputedStyle& decorating_style_;
  const AppliedTextDecoration& decoration_;
  const SimpleFontData* font_data_;
  ScalarPoint origin_;
  float width_;
  float size_;
  float ascent_;
  float thickness_;
  float target_ascent_;
  float decorating_offset_;
  ResolvedUnderlinePosition position_;
  bool flipped_;
  bool underline_;
  bool overline_;
  bool antialias_ = false;
};
} // namespace bkit
