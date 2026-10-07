// Ported from: blink/renderer/core/paint/text_decoration_info.cc
// Ported from: blink/renderer/core/layout/text_decoration_offset.cc
#include "text_decoration_info.h"

#include <algorithm>
#include <cmath>
#include <unicode/uscript.h>

#include "font/simple_font_data.h"
#include "geometry/length_functions.h"

namespace bkit {
namespace {
bool Has(TextDecorationLine lines, TextDecorationLine line) { return (lines & line) != TextDecorationLine::kNone; }
bool Has(TextUnderlinePosition value, TextUnderlinePosition flag) {
  return (static_cast<unsigned>(value) & static_cast<unsigned>(flag)) != 0;
}
ResolvedUnderlinePosition ResolvePosition(const ComputedStyle& style) {
  const auto position = style.GetTextUnderlinePosition();
  if (style.GetFontBaseline() != kCentralBaseline) {
    if (Has(position, TextUnderlinePosition::kUnder)) return ResolvedUnderlinePosition::kUnder;
    if (Has(position, TextUnderlinePosition::kFromFont)) return ResolvedUnderlinePosition::kFromFont;
    return ResolvedUnderlinePosition::kAuto;
  }
  const auto script = style.GetFontDescription().GetScript();
  if (script == USCRIPT_KATAKANA_OR_HIRAGANA || script == USCRIPT_HANGUL) {
    return Has(position, TextUnderlinePosition::kLeft) ? ResolvedUnderlinePosition::kUnder : ResolvedUnderlinePosition::kOver;
  }
  return Has(position, TextUnderlinePosition::kRight) ? ResolvedUnderlinePosition::kOver : ResolvedUnderlinePosition::kUnder;
}
float OffsetPixels(const Length& length, float size) {
  return length.IsAuto() ? 0 : FloatValueForLength(length, size);
}
} // namespace

TextDecorationInfo::TextDecorationInfo(ScalarPoint origin, float width, const ComputedStyle& style,
                                       const Font& font, const AppliedTextDecoration& decoration, const DecoratingBox* box)
    : style_(style),
      decorating_style_(box && box->style->IsHorizontalWritingMode() ? *box->style : style),
      decoration_(decoration),
      font_data_((&decorating_style_ == &style ? font : *decorating_style_.GetFont()).PrimaryFont()),
      origin_(origin), width_(width),
      size_((&decorating_style_ == &style ? font : *decorating_style_.GetFont()).GetFontDescription().ComputedSize()),
      ascent_(font_data_ ? font_data_->GetFontMetrics().FloatAscent() : 0),
      thickness_(size_ / 10),
      target_ascent_(font.PrimaryFont() ? font.PrimaryFont()->GetFontMetrics().FloatAscent() : 0),
      decorating_offset_(box && box->style->IsHorizontalWritingMode() ? box->content_top - origin.y : 0),
      position_(ResolvePosition(decorating_style_)),
      flipped_(position_ == ResolvedUnderlinePosition::kOver),
      underline_(Has(decoration.Lines(), TextDecorationLine::kUnderline)),
      overline_(Has(decoration.Lines(), TextDecorationLine::kOverline)) {
  if (flipped_) { std::swap(underline_, overline_); position_ = ResolvedUnderlinePosition::kUnder; }
  const auto thickness = decoration.Thickness();
  if (!thickness.IsAuto() && font_data_) {
    thickness_ = thickness.IsFromFont() ? font_data_->GetFontMetrics().UnderlineThickness().value_or(thickness_) :
        std::round(FloatValueForLength(thickness.Thickness(), size_));
  }
  thickness_ = HasError() ? decorating_style_.EffectiveZoom() : std::max(1.0f, thickness_);
  for (const auto& line : style.AppliedTextDecorations()) {
    if (line.Style() == ETextDecorationStyle::kDotted || line.Style() == ETextDecorationStyle::kDashed) antialias_ = true;
  }
}
bool TextDecorationInfo::HasLineThrough() const { return Has(decoration_.Lines(), TextDecorationLine::kLineThrough); }
bool TextDecorationInfo::HasError() const {
  return Has(decoration_.Lines(), TextDecorationLine::kSpellingError | TextDecorationLine::kGrammarError);
}
float TextDecorationInfo::Baseline() const { return origin_.y + target_ascent_; }
float TextDecorationInfo::UnderOffset(const Length& offset, FontVerticalPositionType position, float computed_size) const {
  if (!font_data_) return 0;
  LayoutUnit pixels = LayoutUnit::FromFloatRound(OffsetPixels(offset, computed_size));
  if (IsLineOverSide(position)) pixels = -pixels;
  const auto baseline = style_.GetFontBaseline();
  const LayoutUnit value = LayoutUnit::FromFloatRound(font_data_->GetFontMetrics().FloatAscent(baseline)) -
      font_data_->VerticalPosition(position, baseline) + pixels;
  if (position == FontVerticalPositionType::TextTop) return value.Floor() - std::floor(thickness_);
  return IsLineOverSide(position) ? value.Floor() - 1 - std::floor(thickness_) : value.Floor() + 1;
}
float TextDecorationInfo::UnderlineOffset(const Length& offset) const {
  if (!font_data_) return 0;
  if (position_ == ResolvedUnderlinePosition::kUnder)
    return UnderOffset(offset, FontVerticalPositionType::BottomOfEmHeight, size_);
  const auto& metrics = font_data_->GetFontMetrics();
  const float pixels = OffsetPixels(offset, size_);
  if (position_ == ResolvedUnderlinePosition::kFromFont && metrics.UnderlinePosition()) {
    return std::round(metrics.FloatAscent() + *metrics.UnderlinePosition() + pixels);
  }
  const float gap = offset.IsAuto() ? std::max(1.0f, std::ceil(thickness_ / 2)) : 0;
  return metrics.Ascent() + gap + std::round(pixels);
}
DecorationGeometry TextDecorationInfo::MakeLine(TextDecorationLine line, float offset) const {
  float double_offset = thickness_ + 1, wave_offset = double_offset;
  if (line == TextDecorationLine::kOverline) double_offset = wave_offset = -double_offset;
  if (line == TextDecorationLine::kLineThrough) { double_offset = std::floor(double_offset); wave_offset = 0; }
  const bool error = line == TextDecorationLine::kSpellingError || line == TextDecorationLine::kGrammarError;
  const float zoom = decorating_style_.EffectiveZoom();
  const WaveDefinition wave{6 * zoom, 5 * zoom, -4.5f * zoom};
  auto geometry = DecorationGeometry::Make(error ? ETextDecorationStyle::kWavy : decoration_.Style(),
      ScalarRect::MakeXYWH(origin_.x, origin_.y + offset, width_, thickness_), double_offset, wave_offset, error ? &wave : nullptr);
  geometry.antialias = antialias_;
  return geometry;
}
DecorationGeometry TextDecorationInfo::Underline() const {
  return MakeLine(TextDecorationLine::kUnderline,
      UnderlineOffset(flipped_ ? Length() : decoration_.UnderlineOffset()) + decorating_offset_);
}
DecorationGeometry TextDecorationInfo::Overline() const {
  return MakeLine(TextDecorationLine::kOverline,
      UnderOffset(flipped_ ? decoration_.UnderlineOffset() : Length(),
                    flipped_ ? FontVerticalPositionType::TopOfEmHeight : FontVerticalPositionType::TextTop,
                    style_.ComputedFontSize()));
}
DecorationGeometry TextDecorationInfo::LineThrough() const {
  return MakeLine(TextDecorationLine::kLineThrough, 2 * ascent_ / 3 - thickness_ / 2);
}
DecorationGeometry TextDecorationInfo::ErrorLine() const {
  return MakeLine(Has(decoration_.Lines(), TextDecorationLine::kSpellingError) ? TextDecorationLine::kSpellingError :
                   TextDecorationLine::kGrammarError, UnderlineOffset(Length()));
}
Color4f TextDecorationInfo::Color() const {
  // Fixed non-platform marker colors, matching the common Blink theme.
  if (Has(decoration_.Lines(), TextDecorationLine::kSpellingError)) return Color4f::FromColor(0xffff0000);
  if (Has(decoration_.Lines(), TextDecorationLine::kGrammarError)) return Color4f::FromColor(0xffc0c0c0);
  return decoration_.GetColor();
}
} // namespace bkit
