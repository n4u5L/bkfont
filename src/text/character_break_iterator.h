// Ported from: blink/renderer/platform/text/character_break_iterator.h
/*
 * Copyright (C) 2006 Lars Knoll <lars@trolltech.com>
 * Copyright (C) 2007, 2011, 2012 Apple Inc. All rights reserved.
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

#include <unicode/brkiter.h>

#include "platform_export.h"
#include "character.h"
#include "layout_locale.h"
#include "base/text/atomic_string.h"
#include "base/text/character_names.h"
#include "base/text/wtf_uchar.h"

namespace bkit {

typedef icu::BreakIterator TextBreakIterator;

const int kTextBreakDone = -1;

// Iterates over "extended grapheme clusters", as defined in UAX #29.
// Note that platform implementations may be less sophisticated - e.g. ICU prior
// to version 4.0 only supports "legacy grapheme clusters".  Use this for
// general text processing, e.g. string truncation.

class CharacterBreakIterator final {

public:
  explicit CharacterBreakIterator(const StringView&);
  explicit CharacterBreakIterator(base::span<const UChar>);
  CharacterBreakIterator(const CharacterBreakIterator&) = delete;
  CharacterBreakIterator& operator=(const CharacterBreakIterator&) = delete;

  int Next();
  int Current();

  bool IsBreak(int offset) const;
  int Preceding(int offset) const;
  int Following(int offset) const;

  bool operator!() const {
    return !is_8bit_ && !iterator_;
  }

private:
  struct ReturnToPool {
    void operator()(void* ptr) const;
  };
  using PooledIterator = std::unique_ptr<icu::BreakIterator, ReturnToPool>;
  class Pool;
  FRIEND_TEST_ALL_PREFIXES(TextBreakIteratorTest, PooledCharacterBreakIterator);

  void CreateIteratorForBuffer(base::span<const UChar>);

  unsigned ClusterLengthStartingAt(unsigned offset) const {
    ;
    // The only Latin-1 Extended Grapheme Cluster is CR LF
    return IsCRBeforeLF(offset) ? 2 : 1;
  }

  bool IsCRBeforeLF(unsigned offset) const {
    ;
    // SAFTEY: second indexing is safe because of length check, but
    // the first is not. Could be made safe by re-ordering.
    return UNSAFE_TODO(charaters8_[offset]) == '\r' && offset + 1 < length_ && UNSAFE_BUFFERS(charaters8_[offset + 1]) == '\n';
  }

  bool IsLFAfterCR(unsigned offset) const {
    ;
    return UNSAFE_TODO(charaters8_[offset]) == '\n' && offset >= 1 && UNSAFE_TODO(charaters8_[offset - 1]) == '\r';
  }

  bool is_8bit_ = false;

  // For 8 bit strings, we implement the iterator ourselves.
  const LChar* charaters8_ = nullptr;
  unsigned offset_ = 0;
  unsigned length_ = 0;

  // For 16 bit strings, we use a TextBreakIterator.
  PooledIterator iterator_;
};

// Counts the number of grapheme clusters. A surrogate pair or a sequence
// of a non-combining character and following combining characters is
// counted as 1 grapheme cluster.
unsigned NumGraphemeClusters(const String&);

// Returns the number of code units that the next grapheme cluster is made of.
unsigned LengthOfGraphemeCluster(const String&, unsigned = 0);

// Returns a list of graphemes cluster at each character using character break
// rules. The `graphemes` and `text` must have the same size.
void GraphemesClusterList(const StringView& text,
                          base::span<unsigned> graphemes);

} // namespace bkit
