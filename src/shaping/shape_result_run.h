// Ported from: blink/renderer/platform/fonts/shaping/shape_result_run.h
/*
 * Copyright (c) 2012 Google Inc. All rights reserved.
 * Copyright (C) 2013 BlackBerry Limited. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "base/gtest_prod_util.h"
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include "base/containers/span.h"
#include <hb.h>

#include <algorithm>
#include <memory>
#include <type_traits>

#include "glyph_data.h"
#include "glyph_data_range.h"
#include "glyph_index_result.h"
#include "glyph_offset_iterator.h"
#include "shape_result.h"

#include "base/vector.h"

namespace bkfont {

class SimpleFontData;

struct ShapeResultRun final
    : public std::enable_shared_from_this<ShapeResultRun> {
public:
  ShapeResultRun(const SimpleFontData* font,
                 hb_direction_t dir,
                 CanvasRotationInVertical canvas_rotation,
                 hb_script_t script,
                 unsigned start_index,
                 unsigned num_glyphs,
                 unsigned num_characters)
      : glyph_data_(
            std::min(num_glyphs, HarfBuzzRunGlyphData::kMaxCharacterIndex + 1)),
        font_data_(font ? font->shared_from_this() : nullptr),
        start_index_(start_index),
        num_characters_(num_characters),
        width_(0.0f),
        script_(script),
        hb_direction_(dir),
        canvas_rotation_(canvas_rotation) {
  }

  ShapeResultRun(const ShapeResultRun& other)
      : glyph_data_(other.glyph_data_),
        font_data_(other.font_data_),
        graphemes_(other.graphemes_),
        start_index_(other.start_index_),
        num_characters_(other.num_characters_),
        width_(other.width_),
        script_(other.script_),
        hb_direction_(other.hb_direction_),
        canvas_rotation_(other.canvas_rotation_) {
  }

  unsigned NumCharacters() const {
    return num_characters_;
  }
  unsigned NumGlyphs() const {
    return glyph_data_.size();
  }
  bool HasLigatures() const {
    return NumGlyphs() < num_characters_;
  }
  hb_direction_t HbDirection() const {
    return static_cast<hb_direction_t>(hb_direction_);
  }
  bool IsLtr() const {
    return HB_DIRECTION_IS_FORWARD(HbDirection());
  }
  bool IsRtl() const {
    return HB_DIRECTION_IS_BACKWARD(HbDirection());
  }
  bool IsHorizontal() const {
    return HB_DIRECTION_IS_HORIZONTAL(HbDirection());
  }
  CanvasRotationInVertical CanvasRotation() const {
    return canvas_rotation_;
  }
  unsigned NextSafeToBreakOffset(unsigned) const;
  unsigned PreviousSafeToBreakOffset(unsigned) const;
  float XPositionForVisualOffset(unsigned, AdjustMidCluster) const;
  float XPositionForOffset(unsigned, AdjustMidCluster) const;
  void CharacterIndexForXPosition(float,
                                  BreakGlyphsOption,
                                  GlyphIndexResult*) const;
  void LimitNumGlyphs(unsigned start_glyph,
                      unsigned* num_glyphs_in_out,
                      unsigned* num_glyphs_removed_out,
                      const bool is_ltr,
                      const hb_glyph_info_t* glyph_infos);

  unsigned StartIndex() const {
    return start_index_;
  }
  unsigned GlyphToCharacterIndex(unsigned i) const {
    return start_index_ + glyph_data_[i].character_index;
  }

  unsigned NumGraphemes(unsigned start, unsigned end) const;

  // For memory reporting.
  size_t ByteSize() const {
    return sizeof(*this) + glyph_data_.ByteSize();
  }

  // Find the range of HarfBuzzRunGlyphData for the specified character index
  // range. This function uses binary search twice, hence O(2 log n).
  GlyphDataRange FindGlyphDataRange(unsigned start_character_index,
                                    unsigned end_character_index) const {
    GlyphDataRange range = GetGlyphDataRange().FindGlyphDataRange(
        IsRtl(),
        start_character_index,
        end_character_index);
    return range;
  }

  // Creates a new ShapeResultRun instance representing a subset of the current
  // run. Returns |nullptr| if there are no glyphs in the specified range.
  std::shared_ptr<ShapeResultRun> CreateSubRun(unsigned start, unsigned end) {

    unsigned number_of_characters = std::min(end - start, num_characters_);
    auto glyphs = FindGlyphDataRange(start, end);
    const unsigned number_of_glyphs = glyphs.size();
    if (!number_of_glyphs) [[unlikely]] {
      return nullptr;
    }

    auto run = std::make_shared<ShapeResultRun>(
        font_data_.get(),
        HbDirection(),
        canvas_rotation_,
        script_,
        start_index_ + start,
        number_of_glyphs,
        number_of_characters);

    run->glyph_data_.CopyFromRange(glyphs);

    InlineLayoutUnit total_advance;
    for (HarfBuzzRunGlyphData& glyph_data : run->glyph_data_) {
      glyph_data.character_index -= start;
      total_advance += glyph_data.advance;
    }

    run->width_ = total_advance;
    run->num_characters_ = number_of_characters;

    return run;
  }

  // Returns new |ShapeResultRun| if |this| and |other| are merged. Otherwise
  // returns null.
  std::shared_ptr<ShapeResultRun> MergeIfPossible(const ShapeResultRun& other) const {
    if (!CanMerge(other)) {
      return nullptr;
    }

    auto run = std::make_shared<ShapeResultRun>(
        font_data_.get(),
        HbDirection(),
        canvas_rotation_,
        script_,
        start_index_,
        glyph_data_.size() + other.glyph_data_.size(),
        num_characters_ + other.num_characters_);
    // Note: We populate |graphemes_| on demand, e.g. hit testing.
    const int index_adjust = other.start_index_ - start_index_;
    if (IsRtl()) [[unlikely]] {
      run->glyph_data_.CopyFrom(other.glyph_data_, glyph_data_);
      auto* const end =
          run->glyph_data_.begin() + other.glyph_data_.size();
      for (auto* it = run->glyph_data_.begin(); it < end; ++it) {
        it->character_index += index_adjust;
      }
    } else {
      run->glyph_data_.CopyFrom(glyph_data_, other.glyph_data_);
      auto* const end = run->glyph_data_.end();
      for (auto* it =
               run->glyph_data_.begin() + glyph_data_.size();
           it < end;
           ++it) {
        it->character_index += index_adjust;
      }
    }
    run->width_ = width_ + other.width_;
    return run;
  }

  // Returns true if |other| can be merged at end of |this|.
  bool CanMerge(const ShapeResultRun& other) const {
    return start_index_ + num_characters_ == other.start_index_ && canvas_rotation_ == other.canvas_rotation_ && font_data_ == other.font_data_ && hb_direction_ == other.hb_direction_ && script_ == other.script_ && glyph_data_.size() + other.glyph_data_.size() < HarfBuzzRunGlyphData::kMaxCharacterIndex + 1;
  }

  void ExpandRangeToIncludePartialGlyphs(int offset, int* from, int* to) const {
    int end = offset + num_characters_;
    int start;

    if (IsLtr()) {
      start = offset + num_characters_;
      for (unsigned i = 0; i < glyph_data_.size(); ++i) {
        int index = offset + glyph_data_[i].character_index;
        if (start == index) {
          continue;
        }
        end = index;
        if (end > *from && start < *to) {
          *from = std::min(*from, start);
          *to = std::max(*to, end);
        }
        end = offset + num_characters_;
        start = index;
      }
    } else {
      start = offset + num_characters_;
      for (unsigned i = 0; i < glyph_data_.size(); ++i) {
        int index = offset + glyph_data_[i].character_index;
        if (start == index) {
          continue;
        }
        if (end > *from && start < *to) {
          *from = std::min(*from, start);
          *to = std::max(*to, end);
        }
        end = start;
        start = index;
      }
    }

    if (end > *from && start < *to) {
      *from = std::min(*from, start);
      *to = std::max(*to, end);
    }
  }

  // Common signatures with RunInfoPart, to templatize algorithms.
  const ShapeResultRun* GetRunInfo() const {
    return this;
  }
  const GlyphDataRange GetGlyphDataRange() const {
    return GlyphDataRange{*this};
  }
  unsigned OffsetToRunStartIndex() const {
    return 0;
  }

  // Collection of |HarfBuzzRunGlyphData| with optional glyph offset
  class GlyphDataCollection final {

  public:
    explicit GlyphDataCollection(unsigned num_glyphs)
        : data_(num_glyphs) {
    }

    GlyphDataCollection(const GlyphDataCollection& other)
        : data_(other.data_),
          offsets_(other.offsets_) {
    }

    unsigned size() const {
      return data_.size();
    }
    bool IsEmpty() const {
      return size() == 0;
    }

    HarfBuzzRunGlyphData& operator[](unsigned index) {
      return data_[index];
    }
    const HarfBuzzRunGlyphData& operator[](unsigned index) const {
      return data_[index];
    }
    const HarfBuzzRunGlyphData& front() const {
      return data_.front();
    }
    HarfBuzzRunGlyphData& back() {
      return data_.back();
    }
    const HarfBuzzRunGlyphData& back() const {
      return data_.back();
    }

    bool HasNonZeroOffsets() const {
      return !offsets_.empty();
    }

    size_t ByteSize() const {
      return sizeof(*this) + size() * sizeof(HarfBuzzRunGlyphData) + sizeof(GlyphOffset) * offsets_.size();
    }

    // The `span` of `GlyphOffset` if `HasNonZeroOffsets()`, or an empty span.
    base::span<const GlyphOffset> Offsets() const {
      return static_cast<base::span<const GlyphOffset>>(offsets_);
    }

    template <bool has_non_zero_glyph_offsets>
    GlyphOffsetIterator<has_non_zero_glyph_offsets> GetOffsets() const {
      return GlyphOffsetIterator<has_non_zero_glyph_offsets>(offsets_);
    }

    // Note: Caller should be adjust |HarfBuzzRunGlyphData.character_index|.
    void CopyFrom(const GlyphDataCollection& other1,
                  const GlyphDataCollection& other2) {

      std::ranges::copy(other1.data_, data_.data());
      std::ranges::copy(other2.data_,
                        data_.data() + other1.size());

      if (other1.HasNonZeroOffsets()) {
        AllocateOffsetsIfNeeded();
        std::ranges::copy(other1.offsets_, offsets_.begin());
      }
      if (other2.HasNonZeroOffsets()) {
        AllocateOffsetsIfNeeded();
        std::ranges::copy(other2.offsets_,
                          offsets_.begin() + other1.size());
      }
    }

    // Note: Caller should be adjust |HarfBuzzRunGlyphData.character_index|.
    void CopyFromRange(const GlyphDataRange& range) {

      std::ranges::copy(range, data_.data());

      if (!range.HasOffsets() || range.IsEmpty()) {
        offsets_.clear();
      } else {
        AllocateOffsets();
        std::ranges::copy(range.Offsets(), offsets_.begin());
      }
    }

    void AddOffsetHeightAt(unsigned index, float delta) {

      AllocateOffsetsIfNeeded();
      offsets_[index].set_y(offsets_[index].y() + delta);
    }

    void AddOffsetWidthAt(unsigned index, float delta) {

      AllocateOffsetsIfNeeded();
      offsets_[index].set_x(offsets_[index].x() + delta);
    }

    void SetOffsetAt(unsigned index, GlyphOffset offset) {
      if (!HasNonZeroOffsets()) {
        if (offset.IsZero()) {
          return;
        }
        AllocateOffsets();
      }
      offsets_[index] = offset;
    }

    // Vector<HarfBuzzRunGlyphData> like functions
    using iterator = HarfBuzzRunGlyphData*;
    using const_iterator = const HarfBuzzRunGlyphData*;
    iterator begin() {
      return data_.data();
    }
    iterator end() {
      return data_.data() + size();
    }
    const_iterator begin() const {
      return data_.data();
    }
    const_iterator end() const {
      return data_.data() + size();
    }

    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    reverse_iterator rbegin() {
      return std::make_reverse_iterator(end());
    }
    reverse_iterator rend() {
      return std::make_reverse_iterator(begin());
    }
    const_reverse_iterator rbegin() const {
      return std::make_reverse_iterator(end());
    }
    const_reverse_iterator rend() const {
      return std::make_reverse_iterator(begin());
    }

    void Reverse() {
      std::reverse(begin(), end());
      offsets_.Reverse();
    }

    void Shrink(unsigned new_size) {

      // Note: To follow Vector<T>::Shrink(), we accept |new_size == size()|
      if (new_size == size()) {
        return;
      }

      data_.Shrink(new_size);
      if (HasNonZeroOffsets()) {
        offsets_.Shrink(new_size);
      }
    }

  private:
    void AllocateOffsets() {

      offsets_.resize(size());
    }

    void AllocateOffsetsIfNeeded() {
      if (!HasNonZeroOffsets()) {
        AllocateOffsets();
      }
    }

    // Note: |offsets_| holds number of elements instead o here to reduce
    // memory usage.
    Vector<HarfBuzzRunGlyphData> data_;
    // |offsets_| holds collection of offset for |data_[i]|.
    // When all offsets are zero, we don't allocate for reducing memory usage.
    Vector<GlyphOffset> offsets_;
  };

private:
  friend class GlyphDataRange;
  friend class HarfBuzzShaper;
  friend class ShapeResult;
  friend class ShapeResultBuffer;
  friend class ShapeResultCursor;
  friend class ShapeResultTest;
  friend class ShapeResultTestInfo;
  friend class ShapeResultView;
  friend class ShapeResultRunTest;
  FRIEND_TEST_ALL_PREFIXES(GlyphDataRangeTest, Data);
  FRIEND_TEST_ALL_PREFIXES(ShapeResultCursorTest, Ltr);
  FRIEND_TEST_ALL_PREFIXES(ShapeResultCursorTest, Rtl);
  FRIEND_TEST_ALL_PREFIXES(ShapeResultRunTest, GlyphDataCopyConstructor);
  FRIEND_TEST_ALL_PREFIXES(ShapeResultRunTest, GlyphDataCopyFromRange);
  FRIEND_TEST_ALL_PREFIXES(ShapeResultRunTest, GlyphDataReverse);
  FRIEND_TEST_ALL_PREFIXES(ShapeResultRunTest, GlyphDataAddOffsetHeightAt);
  FRIEND_TEST_ALL_PREFIXES(ShapeResultRunTest, GlyphDataAddOffsetWidthAt);
  FRIEND_TEST_ALL_PREFIXES(ShapeResultRunTest, GlyphDataSetAt);
  FRIEND_TEST_ALL_PREFIXES(ShapeResultRunTest, GlyphDataShrink);

  GlyphDataCollection glyph_data_;
  std::shared_ptr<const SimpleFontData> font_data_;

  // graphemes_[i] is the number of graphemes up to (and including) the ith
  // character in the run.
  std::shared_ptr<Vector<unsigned>> graphemes_;

  unsigned start_index_;
  unsigned num_characters_;
  float width_;

  hb_script_t script_;
  uint8_t hb_direction_; // hb_direction_t

  // For upright-in-vertical we need to tell the ShapeResultBloberizer to rotate
  // the canvas back 90deg for this ShapeResultRun.
  CanvasRotationInVertical canvas_rotation_;
};

} // namespace bkfont
