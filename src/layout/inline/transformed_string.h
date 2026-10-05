// Ported from: blink/renderer/core/layout/inline/transformed_string.h
// Ported from: blink/renderer/core/layout/inline/transformed_string.cc
// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include <cassert>
#include <span>

#include "base/text/string_view.h"
#include "base/text/text_offset_map.h"

namespace bkfont {

// A string view with a TextOffsetMap::Length map: the Nth element is the
// source length of the Nth character. An empty map means identity.
class TransformedString {
public:
  explicit TransformedString(StringView view) : view_(view) {}
  TransformedString(StringView view, std::span<const TextOffsetMap::Length> map) : view_(view), length_map_(map) {}

  const StringView& View() const { return view_; }
  bool HasLengthMap() const { return !length_map_.empty(); }
  const std::span<const TextOffsetMap::Length>& LengthMap() const { return length_map_; }

  TransformedString Substring(unsigned start, unsigned length) const {
    StringView sub_view = StringView(view_, start, length);
    if (length_map_.empty()) return TransformedString(sub_view);
    assert(view_.length() == length_map_.size());
    assert(start <= view_.length());
    assert(start + length <= view_.length());
    return TransformedString(sub_view, length_map_.subspan(start, length));
  }
  TransformedString Substring(unsigned start) const { return Substring(start, view_.length() - start); }

private:
  const StringView view_;
  const std::span<const TextOffsetMap::Length> length_map_;
};

} // namespace bkfont
