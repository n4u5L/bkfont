// Ported from: blink/renderer/core/layout/inline/offset_mapping.h
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
//
// Model adaptation: the "DOM" is the InlineObject tree. A position is an
// InlinePosition: a UTF-16 offset in a text object, or 0/1 (before/after) for
// an atomic object, as Blink's offset-in-anchor and before/after-anchor
// positions. Every mapping unit has an associated object; there is no CSS
// generated content in the mapping.
#pragma once

#include <optional>
#include <span>
#include <utility>

#include "base/hash_map.h"
#include "base/heap_vector.h"
#include "base/text/wtf_string.h"
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

enum class OffsetMappingUnitType { kIdentity, kCollapsed, kVariable };

// An OffsetMappingUnit indicates a "simple" offset mapping between dom offset
// range [dom_start, dom_end] on object |owner| and text content offset range
// [text_content_start, text_content_end]. The mapping between them falls in one
// of the following categories, depending on |type|:
// - kIdentity: The mapping between the two ranges is the identity mapping. In
//   other words, the two ranges have the same length, and the offsets are
//   mapped one-to-one.
// - kCollapsed: The mapping is collapsed, namely, |text_content_start| and
//   |text_content_end| are the same, and characters in the dom range are
//   collapsed.
// - kVariable: The mapping is expanded or shrunk, namely.
//   -- |dom_end == dom_start + 1|, and
//      |text_content_end > text_content_start + 1|, indicating that the
//      character in the dom range is expanded into multiple characters, or
//   -- |dom_end > dom_start + 1|, and
//      |text_content_end == text_content_start + 1|, indicating that multiple
//      characters in the dom range is shrunk into a single character.
class OffsetMappingUnit {
public:
  OffsetMappingUnit(OffsetMappingUnitType, const InlineObject&, unsigned dom_start, unsigned dom_end,
                    unsigned text_content_start, unsigned text_content_end);

  // The object whose offsets this unit maps. Never null in this model.
  const InlineObject* AssociatedNode() const {
    return layout_object_;
  }

  OffsetMappingUnitType GetType() const {
    return type_;
  }
  // Returns true if GetType() is OffsetMappingUnitType::kCollapsed.
  bool IsCollapsed() const {
    return type_ == OffsetMappingUnitType::kCollapsed;
  }

  const InlineObject& GetLayoutObject() const {
    return *layout_object_;
  }
  const InlineObject& GetOwner() const {
    return *layout_object_;
  }
  unsigned DOMStart() const {
    return dom_start_;
  }
  unsigned DOMEnd() const {
    return dom_end_;
  }
  unsigned TextContentStart() const {
    return text_content_start_;
  }
  unsigned TextContentEnd() const {
    return text_content_end_;
  }

  // If the passed unit can be concatenated to |this| to create a bigger unit,
  // replaces |this| by the result and returns true; Returns false otherwise.
  bool Concatenate(const OffsetMappingUnit&);

  unsigned ConvertDOMOffsetToTextContent(unsigned) const;

  unsigned ConvertTextContentToFirstDOMOffset(unsigned) const;
  unsigned ConvertTextContentToLastDOMOffset(unsigned) const;

private:
  OffsetMappingUnitType type_ = OffsetMappingUnitType::kIdentity;

  const InlineObject* layout_object_;
  unsigned dom_start_;
  unsigned dom_end_;

  // |text_content_start_| and |text_content_end_| are offsets in
  // |OffsetMapping::text_|. These values are in [0, |text_.length()] to
  // represent collapsed spaces at the end of block.
  unsigned text_content_start_;
  unsigned text_content_end_;

  friend class OffsetMappingBuilder;
};

// Each inline formatting context has an OffsetMapping object that stores the
// mapping information between model positions and offsets in the text content
// string of the context.
class OffsetMapping final {
public:
  using UnitVector = HeapVector<OffsetMappingUnit>;
  // Keyed by object identity, as Blink keys by Node.
  using RangeMap = HashMap<InlineNodeId, std::pair<unsigned, unsigned>>;
  // Local adapter for model positions that Blink's editing canonicalizes
  // before it reaches OffsetMapping: child boundaries of inline containers,
  // and the single offset of an empty text object (which has no units).
  using BoundaryMap = HashMap<InlineNodeId, Vector<unsigned>>;

  OffsetMapping(UnitVector&&, RangeMap&&, String, BoundaryMap&&);
  OffsetMapping(const OffsetMapping&) = delete;
  OffsetMapping& operator=(const OffsetMapping&) = delete;

  const UnitVector& GetUnits() const {
    return units_;
  }
  const RangeMap& GetRanges() const {
    return ranges_;
  }
  const String& GetText() const {
    return text_;
  }

  // ------ Mapping APIs from model positions to text content ------

  // Returns the OffsetMappingUnit whose DOM range contains the position.
  // If there are multiple qualifying units, returns the last one.
  const OffsetMappingUnit* GetMappingUnitForPosition(const InlinePosition&) const;

  // Returns all OffsetMappingUnits whose DOM ranges has non-empty (but
  // possibly collapsed) intersections with the passed in DOM range. If a unit
  // partially intersects the range, it is clamped with only the part within the
  // range returned. Positions in different objects or reversed ranges return
  // no units.
  UnitVector GetMappingUnitsForDOMRange(const InlinePosition& start, const InlinePosition& end) const;

  // Returns all OffsetMappingUnits associated to |object|.
  std::span<const OffsetMappingUnit> GetMappingUnitsForNode(const InlineObject&) const;

  // Returns the text content offset corresponding to the given position.
  // Returns nullopt when the position is not laid out in this context. Also
  // accepts the local container and empty-text boundary positions.
  std::optional<unsigned> GetTextContentOffset(const InlinePosition&) const;

  // Starting from the given position, searches for non-collapsed content in
  // the anchor object in forward/backward direction and returns the position
  // before/after it; Returns null if there is no more non-collapsed content in
  // the anchor object.
  InlinePosition StartOfNextNonCollapsedContent(const InlinePosition&) const;
  InlinePosition EndOfLastNonCollapsedContent(const InlinePosition&) const;

  // Returns true if the position is right before/after non-collapsed content in
  // the anchor object. Note that false is returned if the position is already
  // at the end/start of the anchor object.
  bool IsBeforeNonCollapsedContent(const InlinePosition&) const;
  bool IsAfterNonCollapsedContent(const InlinePosition&) const;

  // Maps the given position to a text content offset, and then returns the text
  // content character before the offset. Returns nullopt if it does not exist.
  std::optional<UChar> GetCharacterBefore(const InlinePosition&) const;

  // ------ Mapping APIs from text content to model positions ------

  // These APIs map a text content offset to model positions, or return null
  // when no object is next to the offset. The returned position is either an
  // offset in a text object, or before/after an atomic object.
  // Note: there can be multiple positions mapped to the same offset when, for
  // example, there are collapsed whitespaces. Hence, we have two APIs to
  // return the first/last one of them.
  InlinePosition GetFirstPosition(unsigned) const;
  InlinePosition GetLastPosition(unsigned) const;

  // InlineCaretPosition::ToPositionInDOMTreeWithAffinity()'s choice: the last
  // position for downstream affinity, the first one for upstream.
  InlinePosition GetPosition(unsigned, TextAffinity = TextAffinity::kDownstream) const;

  // Returns all OffsetMappingUnits whose text content ranges has non-empty
  // (but possibly collapsed) intersection with (start, end). Note that units
  // that only "touch" |start| or |end| are excluded. Reversed ranges return
  // no units.
  std::span<const OffsetMappingUnit> GetMappingUnitsForTextContentOffsetRange(unsigned start,
                                                                              unsigned end) const;

  // Finds the first unit with TextContentEnd() >= |offset|. If its next unit
  // starts at |offset|, returns that next unit instead, as Blink does at a
  // collapsed boundary.
  const OffsetMappingUnit* GetFirstMappingUnit(unsigned offset) const;

  // Returns the last unit whose text content range contains |offset|.
  const OffsetMappingUnit* GetLastMappingUnit(unsigned offset) const;

  // ------ APIs inspecting the text content string ------

  // Returns true if all characters in [start, end) of |text_| are bidi
  // control characters (also true for an empty range).
  bool HasBidiControlCharactersOnly(unsigned start, unsigned end) const;

private:
  // The OffsetMappingUnits of the inline formatting context in sorted order.
  UnitVector units_;

  // Stores the unit range for each object in inline formatting context.
  RangeMap ranges_;

  // The text content string of the inline formatting context.
  String text_;

  BoundaryMap boundaries_;
};

} // namespace bkfont
