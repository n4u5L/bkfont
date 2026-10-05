// Ported from: blink/renderer/core/paint/text_fragment_painter.cc
// Ported from: blink/renderer/core/paint/box_fragment_painter.cc
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once
#include <span>

#include "paint_canvas.h"
#include "geometry/physical_offset.h"
#include "layout/geometry/physical_rect.h"
#include "text/writing_mode.h"

namespace bkfont {

class Font;
class PlatformPaint;
class TextCombine;
class ComputedStyle;
struct TextFragmentPaintInfo;
struct DecoratingBox;

// PhysicalBoxRect() from text_fragment_painter.cc for a non-SVG text item.
// `rect_in_container` is the rect of the text item in its inline formatting
// context root, `paint_offset` the paint offset of that root, and
// `parent_offset` the offset of the line box in the root.
PhysicalRect PhysicalBoxRect(const PhysicalRect& rect_in_container,
                             const PhysicalOffset& paint_offset,
                             const PhysicalOffset& parent_offset,
                             const TextCombine* text_combine);

// The text painting steps of TextFragmentPainter::Paint() for the foreground
// phase: rotates the canvas into line-relative space for a vertical writing
// mode, places the text origin at the alphabetic ascent of the primary font
// below the line-over edge of `physical_box`, and draws the text. Upright
// runs are counter-rotated by the bloberizer (CanvasRotationInVertical).
//
// `font` is the scaled font of the text item; text in `text_combine` uses its
// compressed font instead when it has one. The ComputedStyle overload also
// paints text shadows, decorations, stroke and emphasis marks. Selection,
// highlights, markers and SVG text are not ported.
void PaintTextFragment(PaintCanvas*,
                       const TextFragmentPaintInfo&,
                       const Font& font,
                       WritingMode,
                       const PhysicalRect& physical_box,
                       const PlatformPaint&,
                       const TextCombine* text_combine = nullptr,
                       NodeId node_id = kInvalidNodeId,
                       const ComputedStyle* text_style = nullptr,
                       std::span<const DecoratingBox> = {});

void PaintTextFragment(PaintCanvas*, const TextFragmentPaintInfo&, const Font&, WritingMode,
                       const PhysicalRect&, const ComputedStyle&, NodeId = kInvalidNodeId,
                       std::span<const DecoratingBox> = {});

// Paints `text_combine` whose box is at `paint_offset`: the
// LayoutTextCombine branch of BoxFragmentPainter::PaintInternal() applies the
// scale and synthetic oblique transform, then each combined text fragment is
// painted as BoxFragmentPainter::PaintLineBoxChildItems() does.
void PaintTextCombine(PaintCanvas*,
                      const TextCombine& text_combine,
                      const PhysicalOffset& paint_offset,
                      const PlatformPaint&,
                      NodeId node_id = kInvalidNodeId);

void PaintTextCombine(PaintCanvas*, const TextCombine&, const PhysicalOffset&,
                      const ComputedStyle&, NodeId = kInvalidNodeId);

} // namespace bkfont
