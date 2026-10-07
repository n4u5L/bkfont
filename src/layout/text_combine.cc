// Ported from: blink/renderer/core/layout/layout_text_combine.cc
// Ported from: blink/renderer/core/layout/inline/inline_node.cc
// Ported from: blink/renderer/core/css/resolver/style_adjuster.cc
// Ported from: blink/renderer/core/layout/length_utils.cc
// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "text_combine.h"

#include <array>
#include <numeric>
#include <utility>

#include "font/font_description.h"
#include "font/font_width_variant.h"
#include "font/simple_font_data.h"
#include "geometry/length.h"
#include "layout/inline_text_metrics.h"
#include "shaping/harfbuzz_shaper.h"
#include "shaping/shape_result.h"
#include "text/bidi_paragraph.h"

namespace bkit {

namespace {

// StyleAdjuster::AdjustStyleForCombinedText() for the font: no letter-spacing
// or word-spacing, and the horizontal orientation of 'writing-mode:
// horizontal-tb'.
FontDescription CombinedTextFontDescription(const FontDescription& parent_description) {
  FontDescription description = parent_description;
  description.SetLetterSpacing(Length::Fixed(0.0f));
  description.SetWordSpacing(/* 'normal' */ Length::Fixed(0.0f));
  description.SetOrientation(FontOrientation::kHorizontal);
  return description;
}

// LineOffsetForTextAlign() from length_utils.cc for 'text-align: center',
// which AdjustStyleForCombinedText() sets.
LayoutUnit LineOffsetForTextAlignCenter(TextDirection direction, LayoutUnit space_left) {
  if (IsLtr(direction)) {
    return (space_left / 2).ClampNegativeToZero();
  }
  // In RTL, trailing spaces appear on the left of the line.
  if (space_left > LayoutUnit()) {
    return (space_left / 2).ClampNegativeToZero();
  }
  // In RTL, wide lines should spill out to the left, same as kRight.
  return space_left;
}

} // namespace

TextCombine::TextCombine(const String& text,
                         const Font& parent_font,
                         std::shared_ptr<FontSelector> font_selector,
                         TextDirection direction,
                         bool parent_has_underline_or_overline)
    : text_(text),
      parent_font_(parent_font),
      font_selector_(std::move(font_selector)),
      direction_(direction),
      parent_has_underline_or_overline_(parent_has_underline_or_overline),
      style_font_(CombinedTextFontDescription(parent_font.GetFontDescription()), font_selector_) {
  // ICU requires a non-null text pointer even for an empty paragraph.
  if (text_.empty()) {
    return;
  }

  // InlineNode::SegmentBidiRuns(): the line holds one item per bidi run.
  // Failing to resolve bidi gives up bidi reordering.
  String text_content = text_;
  text_content.Ensure16Bit();
  BidiParagraph bidi;
  BidiParagraph::Runs runs;
  if (bidi.SetParagraph(text_content, direction_)) {
    bidi.GetVisualRuns(text_content, &runs);
  } else if (!text_content.empty()) {
    runs.emplace_back(0, text_content.length(), 0);
  }
  items_.reserve(runs.size());
  for (const BidiParagraph::Run& run : runs) {
    items_.push_back({run.start, run.end, run.Direction(), nullptr, PhysicalRect()});
  }

  ShapeText(style_font_);
  AdjustFontForTextCombineUprightAll();
  PlaceItems();
}

void TextCombine::ShapeText(const Font& font) {
  HarfBuzzShaper shaper(text_);
  for (TextItem& item : items_) {
    item.shape_result = shaper.Shape(&font, item.direction, item.start, item.end);
  }
}

float TextCombine::CalculateWidthForTextCombine() const {
  return std::accumulate(items_.begin(), items_.end(), 0.0f, [](float sum, const TextItem& item) {
    if (item.shape_result) {
      return item.shape_result->Width() + sum;
    }
    return 0.0f;
  });
}

void TextCombine::AdjustFontForTextCombineUprightAll() {
  const float content_width = CalculateWidthForTextCombine();
  if (content_width == 0.0f) [[unlikely]] {
    return; // See "fast/css/zero-font-size-crash.html".
  }
  const float desired_width = DesiredWidth();
  ResetLayout();
  if (desired_width == 0.0f) [[unlikely]] {
    // See http://crbug.com/1342520
    return;
  }
  if (content_width <= desired_width) {
    return;
  }

  FontDescription description = style_font_.GetFontDescription();

  // Try compressed fonts.
  static const std::array<FontWidthVariant, 3> kWidthVariants = {kHalfWidth, kThirdWidth, kQuarterWidth};
  for (const auto width_variant : kWidthVariants) {
    description.SetWidthVariant(width_variant);
    Font compressed_font(description, font_selector_);
    // TODO(crbug.com/561873): PrimaryFont should not be nullptr.
    if (!compressed_font.PrimaryFont()) {
      continue;
    }
    ShapeText(compressed_font);
    if (CalculateWidthForTextCombine() <= desired_width) {
      SetCompressedFont(compressed_font);
      return;
    }
  }

  // There is no compressed font to fit within 1em. We use original font with
  // scaling.
  ShapeText(style_font_);
  SetScaleX(desired_width / content_width);
}

void TextCombine::PlaceItems() {
  // The line never wraps (LineBreaker::disallow_auto_wrap_), and each text
  // item is as wide as its snapped advance.
  LayoutUnit line_width;
  for (const TextItem& item : items_) {
    if (item.shape_result) {
      line_width += item.shape_result->SnappedWidth().ClampNegativeToZero();
    }
  }

  // InlineLayoutAlgorithm::ApplyTextAlign() within the content box, whose
  // inline size is the box width.
  LayoutUnit inline_offset = LineOffsetForTextAlignCenter(direction_, Size().width - line_width);

  // LogicalLineBuilder places combined text at block offset 0 with 1em
  // height. The painter paints text at block offset +
  // |font.internal_leading / 2|.
  const LayoutUnit one_em = ComputedFontSizeAsFixed(style_font_);
  const LayoutUnit text_top = LayoutUnit();
  for (TextItem& item : items_) {
    const LayoutUnit inline_size =
        item.shape_result ? item.shape_result->SnappedWidth().ClampNegativeToZero() : LayoutUnit();
    item.rect = PhysicalRect(inline_offset, text_top, inline_size, one_em);
    inline_offset += inline_size;
  }
}

float TextCombine::DesiredWidth() const {
  const float one_em = style_font_.GetFontDescription().ComputedSize();
  if (parent_has_underline_or_overline_) {
    return one_em;
  }
  // Allow em + 10% margin if there are no underline and overeline for
  // better looking. This isn't specified in the spec[1], but EPUB group
  // wants this.
  // [1] https://www.w3.org/TR/css-writing-modes-3/
  constexpr float kTextCombineMargin = 1.1f;
  return one_em * kTextCombineMargin;
}

PhysicalSize TextCombine::Size() const {
  // StyleAdjuster::AdjustStyleForTextCombine() sizes the box from the parent
  // font before the box switches to the horizontal writing mode.
  const SimpleFontData* font_data = parent_font_.PrimaryFont();
  const LayoutUnit line_height = font_data ? LayoutUnit(font_data->GetFontMetrics().Height()) : LayoutUnit();
  const LayoutUnit one_em = ComputedFontSizeAsFixed(parent_font_);
  return PhysicalSize(line_height, one_em);
}

FontHeight TextCombine::BaselineMetrics(FontBaseline baseline_type) const {
  // LogicalBoxFragment::FirstBaseline() has no value for an orthogonal box.
  return SynthesizeBaselineMetrics(Size().width, baseline_type);
}

float TextCombine::ComputeInlineSpacing() const {
  const SimpleFontData* font_data = style_font_.PrimaryFont();
  const LayoutUnit line_height =
      font_data
          ? font_data->GetFontMetrics().GetFontHeight(GetFontBaseline(style_font_.GetFontDescription())).LineHeight()
          : FontHeight().LineHeight();
  return (line_height - DesiredWidth()) / 2;
}

void TextCombine::ResetLayout() {
  compressed_font_.reset();
  scale_x_.reset();
}

LayoutUnit TextCombine::AdjustTextLeftForPaint(LayoutUnit position) const {
  if (!scale_x_) {
    return position;
  }
  const float spacing = ComputeInlineSpacing();
  return LayoutUnit(position + spacing / *scale_x_);
}

LayoutUnit TextCombine::AdjustTextTopForPaint(LayoutUnit text_top) const {
  const SimpleFontData& font_data = *style_font_.PrimaryFont();
  const float internal_leading = font_data.InternalLeading();
  const float half_leading = internal_leading / 2;
  const int ascent = font_data.GetFontMetrics().Ascent();
  return LayoutUnit(text_top + ascent - half_leading);
}

AffineTransform TextCombine::ComputeAffineTransformForPaint(const PhysicalOffset& paint_offset) const {
  AffineTransform matrix;
  if (UsingSyntheticOblique()) {
    const LayoutUnit text_left = AdjustTextLeftForPaint(paint_offset.left);
    const LayoutUnit text_top = AdjustTextTopForPaint(paint_offset.top);
    matrix.Translate(text_left, text_top);
    // TODO(yosin): We should use angle specified in CSS instead of
    // constant value -15deg. See also |DrawBlobs()| in [1] for vertical
    // upright oblique.
    // [1] "third_party/blink/renderer/platform/fonts/font.cc"
    constexpr float kSlantAngle = -15.0f;
    matrix.SkewY(kSlantAngle);
    matrix.Translate(-text_left, -text_top);
  }
  if (scale_x_.has_value()) {
    matrix.Translate(paint_offset.left, paint_offset.top);
    matrix.Scale(*scale_x_, 1.0f);
    matrix.Translate(-paint_offset.left, -paint_offset.top);
  }
  return matrix;
}

bool TextCombine::NeedsAffineTransformInPaint() const {
  return scale_x_.has_value() || UsingSyntheticOblique();
}

LineRelativeRect TextCombine::ComputeTextFrameRect(const PhysicalOffset paint_offset) const {
  const LayoutUnit one_em = ComputedFontSizeAsFixed(parent_font_);
  const SimpleFontData* font_data = parent_font_.PrimaryFont();
  const FontHeight text_metrics =
      font_data ? font_data->GetFontMetrics().GetFontHeight(GetFontBaseline(parent_font_.GetFontDescription()))
                : FontHeight();
  const LayoutUnit line_height = text_metrics.LineHeight();
  return {LineRelativeOffset::CreateFromBoxOrigin(paint_offset), LogicalSize(one_em, line_height)};
}

void TextCombine::SetScaleX(float new_scale_x) {
  // Note: Even if rounding, e.g. LayoutUnit::FromFloatRound(), we still have
  // gap between painted characters in text-combine-upright-value-all-002.html
  scale_x_ = new_scale_x;
}

void TextCombine::SetCompressedFont(const Font& font) {
  compressed_font_ = font;
}

bool TextCombine::UsingSyntheticOblique() const {
  return parent_font_.GetFontDescription().IsSyntheticOblique();
}

} // namespace bkit
