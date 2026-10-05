// Model equivalent of blink/renderer/core/layout/inline/offset_mapping.h.
// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include <optional>
#include <unordered_map>

#include "base/heap_vector.h"
#include "base/vector.h"
#include "layout/inline/inline_object.h"

namespace bkfont {

enum class TextAffinity {
  kUpstream,
  kDownstream
};

// Persistent positions contain model identity and a UTF-16 offset only.
// Atomic objects use offsets 0/1; inline containers use child boundaries.
struct InlinePosition {
  InlineNodeId node = 0;
  unsigned offset = 0;
  TextAffinity affinity = TextAffinity::kDownstream;
  explicit operator bool() const {
    return node != 0;
  }
  bool operator==(const InlinePosition&) const = default;
};

struct InlineSelection {
  InlinePosition anchor;
  InlinePosition focus;
};

class OffsetMapping {
public:
  struct Unit {
    const InlineObject* object;
    unsigned start;
    unsigned end;
  };
  const String& GetText() const {
    return text_;
  }
  const HeapVector<Unit>& Units() const {
    return units_;
  }
  std::optional<unsigned> GetTextContentOffset(const InlinePosition&) const;
  InlinePosition GetPosition(unsigned, TextAffinity = TextAffinity::kDownstream) const;
  const Unit* GetUnit(InlineNodeId) const;
  bool HasBidiControlCharactersOnly(unsigned start, unsigned end) const;

private:
  friend class InlineFormattingContext;
  friend class InlineLayoutAlgorithm;
  String text_;
  HeapVector<Unit> units_;
  std::unordered_map<InlineNodeId, size_t> index_;
  std::unordered_map<InlineNodeId, Vector<unsigned>> container_offsets_;
};

} // namespace bkfont
