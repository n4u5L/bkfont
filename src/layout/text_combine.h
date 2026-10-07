// Ported from: blink/renderer/core/layout/layout_text_combine.h
// Ported from: blink/renderer/core/layout/inline/inline_node.cc
// Ported from: blink/renderer/core/css/resolver/style_adjuster.cc
// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "base/text/wtf_string.h"
#include "font/font.h"
#include "font/font_baseline.h"
#include "font/font_height.h"
#include "geometry/physical_offset.h"
#include "geometry/physical_size.h"
#include "layout/geometry/physical_rect.h"
#include "paint/line_relative_rect.h"
#include "layout/layout_unit.h"
#include "text/text_direction.h"
#include "paint/affine_transform.h"

namespace bkit {

class FontSelector;
class ShapeResult;

// The box of an element having "text-combine-upright: all" in vertical writing
// mode, e.g. <i style="text-combine-upright: all"><b>12</b>34</i>. Ports the
// parts of LayoutTextCombine, InlineNode::AdjustFontForTextCombineUprightAll()
// and StyleAdjuster::AdjustStyleForTextCombine() that size the box, fit the
// combined text into 1em, and place it for painting.
//
// The combined text is laid out horizontally in one line that never wraps.
// When it is wider than DesiredWidth(), the compressed fonts using the 'hwid',
// 'twid' and 'qwid' OpenType features are tried in order; when none of them
// fits, the original font is used and painting scales the text horizontally.
//
// The text content is used as given: white-space collapsing, control items and
// text decoration ink overflow are not ported.
class TextCombine {
public:
  // A combined text fragment: a bidi run of the single line, in visual order.
  struct TextItem {
    unsigned start;
    unsigned end;
    TextDirection direction;
    std::shared_ptr<const ShapeResult> shape_result;
    // The rect in the combined box, as FragmentItem::RectInContainerFragment().
    PhysicalRect rect;
  };

  // `parent_font` is the font of the parent element in a vertical writing
  // mode, created with `font_selector`. `direction` is the 'direction' of the
  // parent. `parent_has_underline_or_overline` tells whether the parent's
  // text decorations in effect include 'underline' or 'overline'.
  TextCombine(const String& text,
              const Font& parent_font,
              std::shared_ptr<FontSelector> font_selector,
              TextDirection direction,
              bool parent_has_underline_or_overline);

  TextCombine(const TextCombine&) = delete;
  TextCombine& operator=(const TextCombine&) = delete;

  float DesiredWidth() const;
  const String& GetTextContent() const {
    return text_;
  }

  // The font of the combined text: the parent font in horizontal orientation
  // without letter-spacing and word-spacing, as AdjustStyleForCombinedText()
  // computes it.
  const Font& StyleFont() const {
    return style_font_;
  }
  const Font* CompressedFont() const {
    return compressed_font_ ? &*compressed_font_ : nullptr;
  }

  bool UsesScaleX() const {
    return scale_x_.has_value();
  }

  // The physical size of the box: the line height of the parent font by 1em,
  // from AdjustStyleForTextCombine().
  PhysicalSize Size() const;

  // The baseline metrics of the box in the parent line. The box is orthogonal
  // to the parent, so its baseline is synthesized from its block size in the
  // parent line, which is its physical width.
  FontHeight BaselineMetrics(FontBaseline baseline_type) const;

  // The combined text fragments in visual order.
  const std::vector<TextItem>& Items() const {
    return items_;
  }

  // Painting
  // |AdjustText{Left,Top}()| are called within affine transformed canvas,
  // e.g. |PaintTextFragment()|.
  LayoutUnit AdjustTextLeftForPaint(LayoutUnit text_left) const;
  LayoutUnit AdjustTextTopForPaint(LayoutUnit text_top) const;

  AffineTransform ComputeAffineTransformForPaint(const PhysicalOffset& paint_offset) const;
  bool NeedsAffineTransformInPaint() const;

  // Returns text frame rect, in logical direction, used with text painters.
  LineRelativeRect ComputeTextFrameRect(const PhysicalOffset paint_offset) const;

private:
  void AdjustFontForTextCombineUprightAll();
  void ShapeText(const Font& font);
  float CalculateWidthForTextCombine() const;
  void PlaceItems();

  void ResetLayout();
  void SetScaleX(float new_scale_x);
  void SetCompressedFont(const Font& font);

  float ComputeInlineSpacing() const;
  bool UsingSyntheticOblique() const;

  const String text_;
  const Font parent_font_;
  const std::shared_ptr<FontSelector> font_selector_;
  const TextDirection direction_;
  const bool parent_has_underline_or_overline_;
  Font style_font_;

  std::vector<TextItem> items_;

  // |scale_x_| holds scale factor to width of text content to 1em. When we
  // use |scale_x_|, we use |style_font_| instead of compressed font.
  std::optional<float> scale_x_;

  // |compressed_font_| hold width variant of |style_font_|.
  std::optional<Font> compressed_font_;
};

} // namespace bkit
