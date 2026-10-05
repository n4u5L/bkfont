// Ported from: blink/renderer/platform/fonts/utf16_text_iterator.h
/*
 * Copyright (C) Research In Motion Limited 2011. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#pragma once

#include "base/containers/span.h"
#include <unicode/utf16.h>

#include <span>

#include "base/text/character_names.h"
#include "base/text/wtf_string.h"

namespace bkfont {

class UTF16TextIterator {

public:
  // The passed in UChar pointer starts at 'offset'. The iterator operates on
  // the range [offset, endOffset].
  // 'length' denotes the maximum length of the UChar array, which might exceed
  // 'endOffset'.
  explicit UTF16TextIterator(base::span<const UChar> characters)
      : characters_(characters.data()),
        characters_end_(characters.data() + characters.size()),
        size_(static_cast<wtf_size_t>(characters.size())) {
  }

  UTF16TextIterator(const UTF16TextIterator&) = delete;
  UTF16TextIterator& operator=(const UTF16TextIterator&) = delete;

  inline bool Consume(UChar32& character) {
    if (offset_ >= size_) {
      return false;
    }

    character = *characters_;
    current_glyph_length_ = 1;
    if (!U16_IS_SURROGATE(character))
      return true;

    return ConsumeSurrogatePair(character);
  }

  void Advance() {
    characters_ += current_glyph_length_;
    offset_ += current_glyph_length_;
  }

  unsigned Offset() const {
    return offset_;
  }
  unsigned Size() const {
    return size_;
  }
  const UChar* Characters() const {
    return characters_;
  }
  const UChar* GlyphEnd() const {
    return characters_ + current_glyph_length_;
  }

private:
  bool IsValidSurrogatePair(UChar32&);
  bool ConsumeSurrogatePair(UChar32&);
  void ConsumeMultipleUChar();

  const UChar* characters_;
  const UChar* const characters_end_;
  unsigned offset_ = 0;
  const unsigned size_;
  unsigned current_glyph_length_ = 0;
};

} // namespace bkfont
