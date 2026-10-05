// Ported from: blink/renderer/platform/fonts/shaping/shape_result_bloberizer.h

// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <span>

#include "base/text/string_view.h"
#include "base/vector.h"
#include "font/canvas_rotation_in_vertical.h"
#include "font/glyph.h"
#include "font/paint_canvas.h"
#include "font/simple_font_data.h"
#include "platform/text_blob.h"
#include "platform_export.h"
#include "shaping/shape_result_buffer.h"
#include "shaping/support/gfx/geometry.h"

namespace bkfont {

class FontDescription;
class PlainTextNode;
class ShapeResultView;
struct GlyphData;
struct TextRunPaintInfo;

class PLATFORM_EXPORT ShapeResultBloberizer {
public:
  enum class Type { kNormal, kTextIntercepts, kEmitText };

  struct FillGlyphsNG;
  struct FillTextEmphasisGlyphsNG;

  struct FillGlyphs;
  struct FillTextEmphasisGlyphs;

  explicit ShapeResultBloberizer(const FontDescription&, Type);
  ShapeResultBloberizer(const ShapeResultBloberizer&) = delete;
  ShapeResultBloberizer& operator=(const ShapeResultBloberizer&) = delete;

  struct BlobInfo {
    BlobInfo(std::shared_ptr<const TextBlob> b, CanvasRotationInVertical r)
        : blob(std::move(b)), rotation(r) {
    }
    std::shared_ptr<const TextBlob> blob;
    CanvasRotationInVertical rotation;
  };

  using BlobBuffer = Vector<BlobInfo, 16>;

  const BlobBuffer& Blobs();
  float Advance() const {
    return advance_;
  }

private:
  void Add(Glyph glyph,
           const SimpleFontData* font_data,
           CanvasRotationInVertical canvas_rotation,
           float h_offset,
           unsigned character_index) {
    // cannot mix x-only/xy offsets
    if (font_data != pending_font_data_ ||
        canvas_rotation != pending_canvas_rotation_) [[unlikely]] {
      CommitPendingRun();
      pending_font_data_ = font_data;
      pending_canvas_rotation_ = canvas_rotation;
    }

    pending_glyphs_.push_back(glyph);
    pending_offsets_.push_back(h_offset);
    if (!current_text_.IsNull()) [[unlikely]] {
      current_character_indexes_.push_back(character_index);
    }
  }

  void Add(Glyph glyph,
           const SimpleFontData* font_data,
           CanvasRotationInVertical canvas_rotation,
           const gfx::Vector2dF& offset,
           unsigned character_index) {
    // cannot mix x-only/xy offsets
    if (font_data != pending_font_data_ ||
        canvas_rotation != pending_canvas_rotation_) [[unlikely]] {
      CommitPendingRun();
      pending_font_data_ = font_data;
      pending_canvas_rotation_ = canvas_rotation;
      const auto& metrics = font_data->GetFontMetrics();
      pending_vertical_baseline_x_offset_ =
          !IsCanvasRotationInVerticalUpright(canvas_rotation)
              ? 0
              : metrics.FloatAscent() - metrics.FloatAscent(kCentralBaseline);
    }

    pending_glyphs_.push_back(glyph);
    pending_offsets_.push_back(offset.x() + pending_vertical_baseline_x_offset_);
    pending_offsets_.push_back(offset.y());
    if (!current_text_.IsNull()) [[unlikely]] {
      current_character_indexes_.push_back(character_index);
    }
  }

  // Whether the FillFastHorizontalGlyphs or
  // AddFastHorizontalGlyphToBloberizer can be used. Only applies for full
  // runs with no vertical offsets, no text intercepts, and not emitting text.
  bool CanUseFastPath(unsigned from,
                      unsigned to,
                      unsigned length,
                      bool has_vertical_offsets);
  bool CanUseFastPath(unsigned from, unsigned to, const ShapeResultView*);
  float FillFastHorizontalGlyphs(const ShapeResultBuffer&, TextDirection);
  float FillFastHorizontalGlyphs(const ShapeResult*, float advance = 0);
  static void AddFastHorizontalGlyphToBloberizer(void* context,
                                                 unsigned,
                                                 Glyph,
                                                 gfx::Vector2dF glyph_offset,
                                                 float advance,
                                                 bool is_horizontal,
                                                 CanvasRotationInVertical,
                                                 const SimpleFontData*);

  float FillGlyphsForResult(const ShapeResult*,
                            const StringView&,
                            unsigned from,
                            unsigned to,
                            float initial_advance,
                            unsigned run_offset);
  static void AddGlyphToBloberizer(void* context,
                                   unsigned character_index,
                                   Glyph,
                                   gfx::Vector2dF glyph_offset,
                                   float advance,
                                   bool is_horizontal,
                                   CanvasRotationInVertical,
                                   const SimpleFontData*);

  void AddEmphasisMark(const GlyphData& emphasis_data,
                       CanvasRotationInVertical canvas_rotation,
                       gfx::PointF glyph_center,
                       float mid_glyph_offset,
                       float letter_spacing);
  static void AddEmphasisMarkToBloberizer(
      void* context,
      unsigned character_index,
      float advance_so_far,
      unsigned graphemes_in_cluster,
      float cluster_advance,
      CanvasRotationInVertical canvas_rotation);

  bool IsSkipInkException(const StringView& text, unsigned character_index);

  void SetText(const StringView& text,
               unsigned from,
               unsigned to,
               std::span<const unsigned> cluster_starts);
  void CommitText();
  void CommitPendingRun();
  void CommitPendingBlob();

  bool HasPendingVerticalOffsets() const;

  const FontDescription& font_description_;
  const Type type_;

  // Current text blob state.
  TextBlobBuilder builder_;
  CanvasRotationInVertical builder_rotation_ =
      CanvasRotationInVertical::kRegular;
  std::size_t builder_run_count_ = 0;

  // Current run state.
  const SimpleFontData* pending_font_data_ = nullptr;
  CanvasRotationInVertical pending_canvas_rotation_ =
      CanvasRotationInVertical::kRegular;
  Vector<Glyph, 1024> pending_glyphs_;
  Vector<float, 1024> pending_offsets_;

  // Reserve a small amount of space for the common case when printing.
  // Allowing this class to grow larger than ~7k impacts user perf.
  Vector<std::uint8_t, 64> pending_utf8_;
  Vector<std::uint32_t, 64> pending_utf8_character_indexes_;
  Vector<unsigned, 64> current_character_indexes_;
  Vector<unsigned, 64> cluster_ends_;
  unsigned cluster_ends_offset_ = 0;
  StringView current_text_;

  float pending_vertical_baseline_x_offset_ = 0;

  // Constructed blobs.
  BlobBuffer blobs_;
  float advance_ = 0;
};

struct PLATFORM_EXPORT ShapeResultBloberizer::FillGlyphsNG
    : public ShapeResultBloberizer {
  FillGlyphsNG(const FontDescription&,
               const StringView&,
               unsigned from,
               unsigned to,
               const ShapeResultView*,
               Type);
};
struct PLATFORM_EXPORT ShapeResultBloberizer::FillTextEmphasisGlyphsNG
    : public ShapeResultBloberizer {
  FillTextEmphasisGlyphsNG(const FontDescription&,
                           const StringView&,
                           unsigned from,
                           unsigned to,
                           const ShapeResultView*,
                           const GlyphData& emphasis_data);
};

struct PLATFORM_EXPORT ShapeResultBloberizer::FillGlyphs
    : public ShapeResultBloberizer {
  FillGlyphs(const FontDescription&,
             const TextRunPaintInfo&,
             const ShapeResultBuffer&,
             Type);
  FillGlyphs(const FontDescription& font_description,
             const PlainTextNode& node,
             Type type);

private:
  template <typename ShapeList>
  void FillGlyphsSlow(StringView text,
                      TextDirection direction,
                      const ShapeList& list,
                      unsigned from,
                      unsigned to);
};

void DrawTextBlobs(const ShapeResultBloberizer::BlobBuffer& blobs,
                   PaintCanvas& canvas,
                   const gfx::PointF& point,
                   const PlatformPaint& flags,
                   NodeId node_id = kInvalidNodeId);

} // namespace bkfont
