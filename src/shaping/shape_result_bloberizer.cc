// Ported from: blink/renderer/platform/fonts/shaping/shape_result_bloberizer.cc

// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shape_result_bloberizer.h"

#include <hb.h>
#include <unicode/utf16.h>
#include <unicode/utf8.h>

#include <algorithm>
#include <utility>

#include "base/containers/adapters.h"
#include "font/font.h"
#include "font/plain_text_node.h"
#include "font/text_run_paint_info.h"
#include "runtime_enabled_features.h"
#include "shaping/caching_word_shaper.h"
#include "shaping/shape_result.h"
#include "shaping/shape_result_view.h"
#include "text/character.h"
#include "text/text_run.h"

namespace bkfont {

namespace {

// A helper for FillGlyphsSlow().
inline const ShapeResult* GetShapeResult(const std::shared_ptr<const ShapeResult>& item) {
  return item.get();
}

// A helper for FillGlyphsSlow().
inline const ShapeResult* GetShapeResult(const PlainTextItem& item) {
  return item.GetShapeResult();
}

} // namespace

ShapeResultBloberizer::ShapeResultBloberizer(
    const FontDescription& font_description,
    Type type)
    : font_description_(font_description), type_(type) {
}

bool ShapeResultBloberizer::HasPendingVerticalOffsets() const {
  // We exclusively store either horizontal/x-only offsets -- in which case
  // m_offsets.size == size, or vertical/xy offsets -- in which case
  // m_offsets.size == size * 2.
  return pending_glyphs_.size() != pending_offsets_.size();
}

void ShapeResultBloberizer::SetText(const StringView& text,
                                    unsigned from,
                                    unsigned to,
                                    std::span<const unsigned> cluster_starts) {
  if (current_text_.IsNull()) {
    CommitPendingRun();
  }

  // Any outstanding 'current' state should have been moved to 'pending'.

  // cluster_ends_ must be at least the size of the source run length, but
  // the run length may be negative (in which case no glyphs will be added).
  if (from < to) {
    cluster_ends_.resize(to - from);
    for (std::size_t i = 0; i < cluster_starts.size() - 1; ++i) {
      cluster_ends_[cluster_starts[i] - from] = cluster_starts[i + 1];
    }
  } else {
    cluster_ends_.Shrink(0);
  }

  cluster_ends_offset_ = from;
  current_text_ = text;
}

void ShapeResultBloberizer::CommitText() {
  if (current_character_indexes_.empty()) {
    return;
  }

  unsigned from = current_character_indexes_[0];
  unsigned to = current_character_indexes_[0];
  for (unsigned character_index : current_character_indexes_) {
    unsigned character_index_end =
        cluster_ends_[character_index - cluster_ends_offset_];
    from = std::min(from, character_index);
    to = std::max(to, character_index_end);
  }

  // Do the UTF-8 conversion here.
  // For each input code point track the location of output UTF-8 code point.

  unsigned current_text_length = current_text_.length();

  unsigned size = to - from;
  Vector<std::uint32_t, 256> pending_utf8_character_index_from_character_index(size);
  if (current_text_.Is8Bit()) {
    const LChar* latin1 = UNSAFE_TODO(current_text_.Characters8());
    wtf_size_t utf8_size = pending_utf8_.size();
    for (unsigned i = from; i < to;) {
      pending_utf8_character_index_from_character_index[i - from] = utf8_size;

      LChar cp = latin1[i++];
      pending_utf8_.Grow(utf8_size + U8_LENGTH(cp));
      U8_APPEND_UNSAFE(pending_utf8_.begin(), utf8_size, cp);
    }
  } else {
    const UChar* utf16 = UNSAFE_TODO(current_text_.Characters16());
    wtf_size_t utf8_size = pending_utf8_.size();
    for (unsigned i = from; i < to;) {
      pending_utf8_character_index_from_character_index[i - from] = utf8_size;

      UChar32 cp;
      U16_NEXT_OR_FFFD(utf16, i, current_text_length, cp);
      pending_utf8_.Grow(utf8_size + U8_LENGTH(cp));
      U8_APPEND_UNSAFE(pending_utf8_.begin(), utf8_size, cp);
    }
  }

  for (unsigned character_index : current_character_indexes_) {
    unsigned index = character_index - from;
    pending_utf8_character_indexes_.push_back(
        pending_utf8_character_index_from_character_index[index]);
  }

  current_character_indexes_.Shrink(0);
}

void ShapeResultBloberizer::CommitPendingRun() {
  if (pending_glyphs_.empty()) {
    return;
  }

  if (pending_canvas_rotation_ != builder_rotation_) {
    // The pending run rotation doesn't match the current blob; start a new
    // blob.
    CommitPendingBlob();
    builder_rotation_ = pending_canvas_rotation_;
  }

  if (!current_character_indexes_.empty()) [[unlikely]] {
    CommitText();
  }

  PlatformFont run_font =
      pending_font_data_->PlatformData().CreatePlatformFont(&font_description_);

  const auto run_size = static_cast<int>(pending_glyphs_.size());
  const auto text_size = static_cast<int>(pending_utf8_.size());
  const auto& buffer = [&]() -> const TextBlobBuilder::RunBuffer& {
    if (HasPendingVerticalOffsets()) {
      if (text_size) {
        return builder_.AllocRunTextPos(run_font, run_size, text_size);
      } else {
        return builder_.AllocRunPos(run_font, run_size);
      }
    } else {
      if (text_size) {
        return builder_.AllocRunTextPosH(run_font, run_size, 0, text_size);
      } else {
        return builder_.AllocRunPosH(run_font, run_size, 0);
      }
    }
  }();
  builder_run_count_ += 1;

  if (text_size) {
    std::ranges::copy(pending_utf8_character_indexes_, buffer.clusters);
    std::ranges::copy(pending_utf8_, buffer.utf8text);

    pending_utf8_.Shrink(0);
    pending_utf8_character_indexes_.Shrink(0);
  }

  std::ranges::copy(pending_glyphs_, buffer.glyphs);
  std::ranges::copy(pending_offsets_, buffer.pos);
  pending_glyphs_.Shrink(0);
  pending_offsets_.Shrink(0);
}

void ShapeResultBloberizer::CommitPendingBlob() {
  if (!builder_run_count_) {
    return;
  }

  blobs_.emplace_back(builder_.Make(), builder_rotation_);
  builder_run_count_ = 0;
}

const ShapeResultBloberizer::BlobBuffer& ShapeResultBloberizer::Blobs() {
  CommitPendingRun();
  CommitPendingBlob();

  return blobs_;
}

inline bool ShapeResultBloberizer::IsSkipInkException(
    const StringView& text,
    unsigned character_index) {
  // We want to skip descenders in general, but it is undesirable renderings
  // for CJK characters.
  return type_ == ShapeResultBloberizer::Type::kTextIntercepts &&
         !Character::CanTextDecorationSkipInk(
             text.CodepointAt(character_index));
}

inline void ShapeResultBloberizer::AddEmphasisMark(
    const GlyphData& emphasis_data,
    CanvasRotationInVertical canvas_rotation,
    PointF glyph_center,
    float mid_glyph_offset,
    float letter_spacing) {
  const SimpleFontData* emphasis_font_data = emphasis_data.font_data.get();

  bool is_vertical =
      emphasis_font_data->PlatformData().IsVerticalAnyUpright() &&
      IsCanvasRotationInVerticalUpright(emphasis_data.canvas_rotation);

  if (!is_vertical) {
    if (RuntimeEnabledFeatures::TextEmphasisLetterSpacingEnabled()) {
      Add(emphasis_data.glyph, emphasis_font_data,
          CanvasRotationInVertical::kRegular,
          mid_glyph_offset - glyph_center.x() - letter_spacing / 2, 0);
    } else {
      Add(emphasis_data.glyph, emphasis_font_data,
          CanvasRotationInVertical::kRegular,
          mid_glyph_offset - glyph_center.x(), 0);
    }
  } else {
    Add(emphasis_data.glyph, emphasis_font_data, emphasis_data.canvas_rotation,
        Vector2dF(-glyph_center.x(), mid_glyph_offset - glyph_center.y()),
        0);
  }
}

namespace {
class GlyphCallbackContext {
public:
  GlyphCallbackContext(ShapeResultBloberizer* bloberizer,
                       const StringView& text)
      : bloberizer(bloberizer), text(text) {
  }
  GlyphCallbackContext(const GlyphCallbackContext&) = delete;
  GlyphCallbackContext& operator=(const GlyphCallbackContext&) = delete;

  ShapeResultBloberizer* bloberizer;
  const StringView& text;
};
} // namespace

/*static*/ void ShapeResultBloberizer::AddGlyphToBloberizer(
    void* context,
    unsigned character_index,
    Glyph glyph,
    Vector2dF glyph_offset,
    float advance,
    bool is_horizontal,
    CanvasRotationInVertical rotation,
    const SimpleFontData* font_data) {
  GlyphCallbackContext* parsed_context =
      static_cast<GlyphCallbackContext*>(context);
  ShapeResultBloberizer* bloberizer = parsed_context->bloberizer;
  const StringView& text = parsed_context->text;

  if (bloberizer->IsSkipInkException(text, character_index)) {
    return;
  }
  Vector2dF start_offset =
      is_horizontal ? Vector2dF(advance, 0) : Vector2dF(0, advance);
  bloberizer->Add(glyph, font_data, rotation, start_offset + glyph_offset,
                  character_index);
}

/*static*/ void ShapeResultBloberizer::AddFastHorizontalGlyphToBloberizer(
    void* context,
    unsigned character_index,
    Glyph glyph,
    Vector2dF glyph_offset,
    float advance,
    bool,
    CanvasRotationInVertical canvas_rotation,
    const SimpleFontData* font_data) {
  ShapeResultBloberizer* bloberizer =
      static_cast<ShapeResultBloberizer*>(context);
  bloberizer->Add(glyph, font_data, canvas_rotation, advance + glyph_offset.x(),
                  character_index);
}

float ShapeResultBloberizer::FillGlyphsForResult(const ShapeResult* result,
                                                 const StringView& text,
                                                 unsigned from,
                                                 unsigned to,
                                                 float initial_advance,
                                                 unsigned run_offset) {
  GlyphCallbackContext context = {this, text};
  return result->ForEachGlyph(initial_advance, from, to, run_offset,
                              AddGlyphToBloberizer,
                              static_cast<void*>(&context));
}

namespace {
class ClusterCallbackContext {
public:
  ClusterCallbackContext(ShapeResultBloberizer* bloberizer,
                         const StringView& text,
                         const GlyphData& emphasis_data,
                         PointF glyph_center,
                         float letter_spacing)
      : bloberizer(bloberizer),
        text(text),
        emphasis_data(emphasis_data),
        glyph_center(std::move(glyph_center)),
        letter_spacing(letter_spacing) {
  }
  ClusterCallbackContext(const ClusterCallbackContext&) = delete;
  ClusterCallbackContext& operator=(const ClusterCallbackContext&) = delete;

  ShapeResultBloberizer* bloberizer;
  const StringView& text;
  const GlyphData& emphasis_data;
  PointF glyph_center;
  float letter_spacing;
};
} // namespace

/*static*/ void ShapeResultBloberizer::AddEmphasisMarkToBloberizer(
    void* context,
    unsigned character_index,
    float advance_so_far,
    unsigned graphemes_in_cluster,
    float cluster_advance,
    CanvasRotationInVertical canvas_rotation) {
  ClusterCallbackContext* parsed_context =
      static_cast<ClusterCallbackContext*>(context);
  ShapeResultBloberizer* bloberizer = parsed_context->bloberizer;
  const StringView& text = parsed_context->text;
  const GlyphData& emphasis_data = parsed_context->emphasis_data;
  PointF glyph_center = parsed_context->glyph_center;

  if (text.Is8Bit()) {
    if (Character::CanReceiveTextEmphasis(text[character_index])) {
      bloberizer->AddEmphasisMark(emphasis_data, canvas_rotation, glyph_center,
                                  advance_so_far + cluster_advance / 2,
                                  parsed_context->letter_spacing);
    }
  } else {
    float glyph_advance_x = cluster_advance / graphemes_in_cluster;
    for (unsigned j = 0; j < graphemes_in_cluster; ++j) {
      // Do not put emphasis marks on space, separator, and control
      // characters.
      if (Character::CanReceiveTextEmphasis(
              text.CodepointAt(character_index))) {
        bloberizer->AddEmphasisMark(emphasis_data, canvas_rotation,
                                    glyph_center,
                                    advance_so_far + glyph_advance_x / 2,
                                    parsed_context->letter_spacing);
      }
      advance_so_far += glyph_advance_x;
    }
  }
}

namespace {
class ClusterStarts {
public:
  ClusterStarts() = default;
  ClusterStarts(const ClusterStarts&) = delete;
  ClusterStarts& operator=(const ClusterStarts&) = delete;

  static void Accumulate(void* context,
                         unsigned character_index,
                         Glyph,
                         Vector2dF,
                         float,
                         bool,
                         CanvasRotationInVertical,
                         const SimpleFontData*) {
    ClusterStarts* self = static_cast<ClusterStarts*>(context);

    if (self->cluster_starts_.empty() ||
        self->last_seen_character_index_ != character_index) {
      self->cluster_starts_.push_back(character_index);
      self->last_seen_character_index_ = character_index;
    }
  }

  void Finish(unsigned, unsigned to) {
    std::sort(cluster_starts_.begin(), cluster_starts_.end());
    cluster_starts_.push_back(to);
  }

  std::span<const unsigned> Data() {
    return std::span<const unsigned>(cluster_starts_.data(), cluster_starts_.size());
  }

private:
  Vector<unsigned, 256> cluster_starts_;
  unsigned last_seen_character_index_ = 0;
};
} // namespace

ShapeResultBloberizer::FillGlyphs::FillGlyphs(
    const FontDescription& font_description,
    const TextRunPaintInfo& run_info,
    const ShapeResultBuffer& result_buffer,
    const Type type)
    : ShapeResultBloberizer(font_description, type) {
  if (CanUseFastPath(run_info.from, run_info.to, run_info.run.length(),
                     result_buffer.HasVerticalOffsets())) {
    advance_ =
        FillFastHorizontalGlyphs(result_buffer, run_info.run.Direction());
    return;
  }

  const auto& results = result_buffer.results_;
  FillGlyphsSlow(run_info.run.ToStringView(), run_info.run.Direction(), results,
                 run_info.from, run_info.to);
}

ShapeResultBloberizer::FillGlyphs::FillGlyphs(
    const FontDescription& font_description,
    const PlainTextNode& node,
    const Type type)
    : ShapeResultBloberizer(font_description, type) {
  const unsigned to = node.TextContent().length();
  if (CanUseFastPath(0, to, to, node.HasVerticalOffsets())) {
    float advance = 0;
    for (const auto& item : node.ItemList()) {
      advance = FillFastHorizontalGlyphs(item.GetShapeResult(), advance);
    }
    advance_ = advance;
    return;
  }

  FillGlyphsSlow(node.TextContent(), node.BaseDirection(), node.ItemList(), 0,
                 to);
}

template <typename ShapeList>
void ShapeResultBloberizer::FillGlyphs::FillGlyphsSlow(StringView text,
                                                       TextDirection direction,
                                                       const ShapeList& list,
                                                       unsigned from,
                                                       unsigned to) {
  if (type_ == Type::kEmitText) [[unlikely]] {
    unsigned word_offset = 0;
    ClusterStarts cluster_starts;
    for (const auto& item : list) {
      const ShapeResult* word_result = GetShapeResult(item);
      word_result->ForEachGlyph(0, from, to, word_offset,
                                ClusterStarts::Accumulate,
                                static_cast<void*>(&cluster_starts));
      word_offset += word_result->NumCharacters();
    }
    cluster_starts.Finish(from, to);
    SetText(text, from, to, cluster_starts.Data());
  }

  float advance = 0;
  if (IsRtl(direction)) {
    unsigned word_offset = text.length();
    for (const auto& item : base::Reversed(list)) {
      const ShapeResult* word_result = GetShapeResult(item);
      unsigned word_characters = word_result->NumCharacters();
      word_offset -= word_characters;
      advance = FillGlyphsForResult(word_result, text, from, to, advance,
                                    word_offset);
    }
  } else {
    unsigned word_offset = 0;
    for (const auto& item : list) {
      const ShapeResult* word_result = GetShapeResult(item);
      unsigned word_characters = word_result->NumCharacters();
      advance = FillGlyphsForResult(word_result, text, from, to, advance,
                                    word_offset);
      word_offset += word_characters;
    }
  }

  if (type_ == Type::kEmitText) [[unlikely]] {
    CommitText();
  }

  advance_ = advance;
}

ShapeResultBloberizer::FillGlyphsNG::FillGlyphsNG(
    const FontDescription& font_description,
    const StringView& text,
    unsigned from,
    unsigned to,
    const ShapeResultView* result,
    const Type type)
    : ShapeResultBloberizer(font_description, type) {
  float initial_advance = 0;
  if (CanUseFastPath(from, to, result)) {
    advance_ = result->ForEachGlyph(initial_advance,
                                    &AddFastHorizontalGlyphToBloberizer,
                                    static_cast<void*>(this));
    return;
  }

  unsigned run_offset = 0;
  if (type_ == Type::kEmitText) [[unlikely]] {
    ClusterStarts cluster_starts;
    result->ForEachGlyph(initial_advance, from, to, run_offset,
                         ClusterStarts::Accumulate,
                         static_cast<void*>(&cluster_starts));
    cluster_starts.Finish(from, to);
    SetText(text, from, to, cluster_starts.Data());
  }

  GlyphCallbackContext context = {this, text};
  advance_ =
      result->ForEachGlyph(initial_advance, from, to, run_offset,
                           AddGlyphToBloberizer, static_cast<void*>(&context));

  if (type_ == Type::kEmitText) [[unlikely]] {
    CommitText();
  }
}

ShapeResultBloberizer::FillTextEmphasisGlyphsNG::FillTextEmphasisGlyphsNG(
    const FontDescription& font_description,
    const StringView& text,
    unsigned from,
    unsigned to,
    const ShapeResultView* result,
    const GlyphData& emphasis)
    : ShapeResultBloberizer(font_description, Type::kNormal) {
  PointF glyph_center =
      emphasis.font_data->BoundsForGlyph(emphasis.glyph).CenterPoint();
  ClusterCallbackContext context = {this, text, emphasis, glyph_center,
                                    font_description.LetterSpacing()};
  float initial_advance = 0;
  unsigned index_offset = 0;
  advance_ = result->ForEachGraphemeClusters(
      text, initial_advance, from, to, index_offset,
      AddEmphasisMarkToBloberizer, static_cast<void*>(&context));
}

bool ShapeResultBloberizer::CanUseFastPath(unsigned from,
                                           unsigned to,
                                           unsigned length,
                                           bool has_vertical_offsets) {
  return !from && to == length && !has_vertical_offsets &&
         type_ != ShapeResultBloberizer::Type::kTextIntercepts &&
         type_ != ShapeResultBloberizer::Type::kEmitText;
}

bool ShapeResultBloberizer::CanUseFastPath(
    unsigned from,
    unsigned to,
    const ShapeResultView* shape_result) {
  return from <= shape_result->StartIndex() && to >= shape_result->EndIndex() &&
         !shape_result->HasVerticalOffsets() &&
         type_ != ShapeResultBloberizer::Type::kTextIntercepts &&
         type_ != ShapeResultBloberizer::Type::kEmitText;
}

float ShapeResultBloberizer::FillFastHorizontalGlyphs(
    const ShapeResultBuffer& result_buffer,
    TextDirection text_direction) {
  float advance = 0;
  const auto& results = result_buffer.results_;

  for (unsigned i = 0; i < results.size(); ++i) {
    const auto& word_result =
        IsLtr(text_direction) ? results[i] : results[results.size() - 1 - i];
    advance = FillFastHorizontalGlyphs(word_result.get(), advance);
  }

  return advance;
}

float ShapeResultBloberizer::FillFastHorizontalGlyphs(const ShapeResult* result,
                                                      float initial_advance) {
  return result->ForEachGlyph(initial_advance,
                              &AddFastHorizontalGlyphToBloberizer,
                              static_cast<void*>(this));
}

void DrawTextBlobs(const ShapeResultBloberizer::BlobBuffer& blobs,
                   PaintCanvas& canvas,
                   const PointF& point,
                   const PlatformPaint& flags,
                   NodeId node_id) {
  for (const auto& blob_info : blobs) {
    PaintCanvasAutoRestore auto_restore(&canvas, false);
    switch (blob_info.rotation) {
    case CanvasRotationInVertical::kRegular:
      break;
    case CanvasRotationInVertical::kRotateCanvasUpright: {
      canvas.Save();

      ScalarMatrix m;
      m.SetSinCos(-1, 0, point.x(), point.y());
      canvas.Concat(m);
      break;
    }
    case CanvasRotationInVertical::kRotateCanvasUprightOblique: {
      canvas.Save();

      ScalarMatrix m;
      m.SetSinCos(-1, 0, point.x(), point.y());
      // TODO(yosin): We should use angle specified in CSS instead of
      // constant value -15deg.
      // Note: We draw glyph in right-top corner upper.
      // See CSS "transform: skew(0, -15deg)"
      ScalarMatrix skew_y;
      constexpr float kSkewY = -0.2679491924311227f; // tan(-15deg)
      skew_y.SetSkew(0, kSkewY, point.x(), point.y());
      m.PreConcat(skew_y);
      canvas.Concat(m);
      break;
    }
    case CanvasRotationInVertical::kOblique: {
      // TODO(yosin): We should use angle specified in CSS instead of
      // constant value 15deg.
      // Note: We draw glyph in right-top corner upper.
      // See CSS "transform: skew(0, -15deg)"
      canvas.Save();
      ScalarMatrix skew_x;
      constexpr float kSkewX = 0.2679491924311227f; // tan(15deg)
      skew_x.SetSkew(kSkewX, 0, point.x(), point.y());
      canvas.Concat(skew_x);
      break;
    }
    }
    if (node_id != kInvalidNodeId) {
      canvas.DrawTextBlob(blob_info.blob, point.x(), point.y(), node_id, flags);
    } else {
      canvas.DrawTextBlob(blob_info.blob, point.x(), point.y(), flags);
    }
  }
}

} // namespace bkfont
