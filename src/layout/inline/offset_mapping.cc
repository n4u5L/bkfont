// Ported from: blink/renderer/core/layout/inline/offset_mapping.cc
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "offset_mapping.h"

#include <algorithm>
#include <cassert>
#include <functional>
#include <iterator>
#include <tuple>

#include "text/character.h"

namespace bkfont {

namespace {

InlinePosition CreatePositionForOffsetMapping(const InlineObject& node, unsigned dom_offset) {
  if (node.IsText()) {
    // 'text-transform' may make the rendered text length longer than the
    // original text node, in which case we clamp the offset to avoid crashing.
    const unsigned clamped_offset = std::min(dom_offset, node.Text().length());
    return {node.Id(), clamped_offset};
  }
  // For non-text-anchored position, the offset must be either 0 or 1.
  return {node.Id(), dom_offset ? 1u : 0u};
}

} // namespace

OffsetMappingUnit::OffsetMappingUnit(OffsetMappingUnitType type, const InlineObject& layout_object,
                                     unsigned dom_start, unsigned dom_end, unsigned text_content_start,
                                     unsigned text_content_end)
    : type_(type),
      layout_object_(&layout_object),
      dom_start_(dom_start),
      dom_end_(dom_end),
      text_content_start_(text_content_start),
      text_content_end_(text_content_end) {
  assert(dom_start_ <= dom_end_);
  assert(text_content_start_ <= text_content_end_);
}

bool OffsetMappingUnit::Concatenate(const OffsetMappingUnit& other) {
  if (layout_object_ != other.layout_object_)
    return false;
  if (type_ != other.type_)
    return false;
  if (dom_end_ != other.dom_start_)
    return false;
  if (text_content_end_ != other.text_content_start_)
    return false;
  dom_end_ = other.dom_end_;
  text_content_end_ = other.text_content_end_;
  return true;
}

unsigned OffsetMappingUnit::ConvertDOMOffsetToTextContent(unsigned offset) const {
  assert(offset >= dom_start_ && offset <= dom_end_);
  // DOM start is always mapped to text content start.
  if (offset == dom_start_)
    return text_content_start_;
  // DOM end is always mapped to text content end.
  if (offset == dom_end_)
    return text_content_end_;
  // Handle collapsed mapping.
  if (text_content_start_ == text_content_end_)
    return text_content_start_;
  // Handle has identity mapping.
  unsigned text_content_offset = offset - dom_start_ + text_content_start_;
  return text_content_offset < text_content_end_ ? text_content_offset : text_content_end_;
}

unsigned OffsetMappingUnit::ConvertTextContentToFirstDOMOffset(unsigned offset) const {
  assert(offset >= text_content_start_ && offset <= text_content_end_);
  // Always return DOM start for collapsed units.
  if (text_content_start_ == text_content_end_)
    return dom_start_;
  // Handle identity mapping.
  if (type_ == OffsetMappingUnitType::kIdentity) {
    return dom_start_ + offset - text_content_start_;
  }
  // Handle expanded mapping.
  return offset < text_content_end_ ? dom_start_ : dom_end_;
}

unsigned OffsetMappingUnit::ConvertTextContentToLastDOMOffset(unsigned offset) const {
  assert(offset >= text_content_start_ && offset <= text_content_end_);
  // Always return DOM end for collapsed units.
  if (text_content_start_ == text_content_end_)
    return dom_end_;
  // In a non-collapsed unit, mapping between DOM and text content offsets is
  // one-to-one. Reuse existing code.
  return ConvertTextContentToFirstDOMOffset(offset);
}

OffsetMapping::OffsetMapping(UnitVector&& units, RangeMap&& ranges, String text, BoundaryMap&& boundaries)
    : units_(std::move(units)),
      ranges_(std::move(ranges)),
      text_(text),
      boundaries_(std::move(boundaries)) {
#ifndef NDEBUG
  for (const auto& unit : units_) {
    assert(unit.TextContentStart() <= text_.length());
    assert(unit.TextContentEnd() <= text_.length());
  }
  for (const auto& range : ranges_) {
    assert(range.value.first < range.value.second);
    assert(range.value.second <= units_.size());
  }
#endif
}

const OffsetMappingUnit* OffsetMapping::GetMappingUnitForPosition(const InlinePosition& position) const {
  const unsigned offset = position.offset;
  unsigned range_start = 0;
  unsigned range_end = 0;
  if (ranges_.IsValidKey(position.node)) {
    const auto it = ranges_.find(position.node);
    if (it != ranges_.end()) std::tie(range_start, range_end) = it->value;
  }
  if (range_start == range_end || units_[range_start].DOMStart() > offset)
    return nullptr;
  // Find the last unit where unit.dom_start <= offset
  const auto range = std::span<const OffsetMappingUnit>(units_.data(), units_.size())
                         .subspan(range_start, range_end - range_start);
  const auto i = std::ranges::upper_bound(range, offset, std::ranges::less(), &OffsetMappingUnit::DOMStart);
  const OffsetMappingUnit* unit = &range[std::distance(range.begin(), i) - 1];
  if (unit->DOMEnd() < offset)
    return nullptr;
  return unit;
}

OffsetMapping::UnitVector OffsetMapping::GetMappingUnitsForDOMRange(const InlinePosition& start,
                                                                    const InlinePosition& end) const {
  // InlinePosition pairs do not have EphemeralRange's ordering guarantee.
  if (start.node != end.node || start.offset > end.offset) return UnitVector();
  const unsigned start_offset = start.offset;
  const unsigned end_offset = end.offset;
  unsigned range_start = 0;
  unsigned range_end = 0;
  if (ranges_.IsValidKey(start.node)) {
    const auto it = ranges_.find(start.node);
    if (it != ranges_.end()) std::tie(range_start, range_end) = it->value;
  }

  if (range_start == range_end || units_[range_start].DOMStart() > end_offset ||
      units_[range_end - 1].DOMEnd() < start_offset)
    return UnitVector();

  const auto units = std::span<const OffsetMappingUnit>(units_.data(), units_.size());
  // Find the first unit where unit.dom_end >= start_offset
  const auto span1 = units.subspan(range_start, range_end - range_start);
  const size_t result_begin =
      range_start + std::distance(span1.begin(), std::ranges::lower_bound(span1, start_offset, std::ranges::less(),
                                                                           &OffsetMappingUnit::DOMEnd));

  // Find the next of the last unit where unit.dom_start <= end_offset
  const auto span2 = units.subspan(result_begin, range_end - result_begin);
  const size_t result_size = std::distance(
      span2.begin(), std::ranges::upper_bound(span2, end_offset, std::ranges::less(), &OffsetMappingUnit::DOMStart));

  UnitVector result;
  result.reserve(static_cast<wtf_size_t>(result_size));
  for (const auto& unit : units.subspan(result_begin, result_size)) {
    // If the unit isn't fully within the range, create a new unit that's
    // within the range.
    const unsigned clamped_start = std::max(unit.DOMStart(), start_offset);
    const unsigned clamped_end = std::min(unit.DOMEnd(), end_offset);
    const unsigned clamped_text_content_start = unit.ConvertDOMOffsetToTextContent(clamped_start);
    const unsigned clamped_text_content_end = unit.ConvertDOMOffsetToTextContent(clamped_end);
    result.emplace_back(unit.GetType(), unit.GetLayoutObject(), clamped_start, clamped_end,
                        clamped_text_content_start, clamped_text_content_end);
  }
  return result;
}

std::span<const OffsetMappingUnit> OffsetMapping::GetMappingUnitsForNode(const InlineObject& node) const {
  if (!ranges_.IsValidKey(node.Id())) return {};
  const auto it = ranges_.find(node.Id());
  if (it == ranges_.end()) {
    return {};
  }
  const auto [first, last] = it->value;
  return std::span<const OffsetMappingUnit>(units_.data(), units_.size()).subspan(first, last - first);
}

std::span<const OffsetMappingUnit> OffsetMapping::GetMappingUnitsForTextContentOffsetRange(unsigned start,
                                                                                         unsigned end) const {
  if (start > end || units_.empty() || units_.front().TextContentStart() >= end ||
      units_.back().TextContentEnd() <= start)
    return {};

  const auto units = std::span<const OffsetMappingUnit>(units_.data(), units_.size());
  // Find the first unit where unit.text_content_end > start
  const auto result_begin =
      std::ranges::lower_bound(units, start, std::less_equal<>{}, &OffsetMappingUnit::TextContentEnd);
  if (result_begin == units.end() || result_begin->TextContentStart() >= end) {
    return {};
  }

  // Find the next of the last unit where unit.text_content_start < end
  const auto result_end =
      std::ranges::upper_bound(units, end, std::less_equal<>{}, &OffsetMappingUnit::TextContentStart);
  return units.subspan(static_cast<size_t>(result_begin - units.begin()),
                       static_cast<size_t>(result_end - result_begin));
}

std::optional<unsigned> OffsetMapping::GetTextContentOffset(const InlinePosition& position) const {
  if (const OffsetMappingUnit* unit = GetMappingUnitForPosition(position))
    return unit->ConvertDOMOffsetToTextContent(position.offset);
  // Local container child boundaries and empty text objects.
  if (!boundaries_.IsValidKey(position.node)) return std::nullopt;
  const auto found = boundaries_.find(position.node);
  if (found != boundaries_.end() && position.offset < found->value.size())
    return found->value[position.offset];
  return std::nullopt;
}

InlinePosition OffsetMapping::StartOfNextNonCollapsedContent(const InlinePosition& position) const {
  const OffsetMappingUnit* unit = GetMappingUnitForPosition(position);
  if (!unit)
    return InlinePosition();

  const InlineObject& node = unit->GetOwner();
  const unsigned offset = position.offset;
  const auto units = std::span<const OffsetMappingUnit>(units_.data(), units_.size());
  for (const auto& u : units.subspan(static_cast<size_t>(unit - units_.data()))) {
    if (u.AssociatedNode() != &node) {
      break;
    }
    if (u.DOMEnd() > offset && !u.IsCollapsed()) {
      const unsigned result = std::max(offset, u.DOMStart());
      return CreatePositionForOffsetMapping(node, result);
    }
  }
  return InlinePosition();
}

InlinePosition OffsetMapping::EndOfLastNonCollapsedContent(const InlinePosition& position) const {
  const OffsetMappingUnit* unit = GetMappingUnitForPosition(position);
  if (!unit)
    return InlinePosition();

  const InlineObject& node = unit->GetOwner();
  const unsigned offset = position.offset;
  for (const OffsetMappingUnit* u = unit;; --u) {
    if (u->AssociatedNode() != &node) {
      break;
    }
    if (u->DOMStart() < offset && !u->IsCollapsed()) {
      const unsigned result = std::min(offset, u->DOMEnd());
      return CreatePositionForOffsetMapping(node, result);
    }
    if (u == units_.data()) break;
  }
  return InlinePosition();
}

bool OffsetMapping::IsBeforeNonCollapsedContent(const InlinePosition& position) const {
  const OffsetMappingUnit* unit = GetMappingUnitForPosition(position);
  const unsigned offset = position.offset;
  return unit && offset < unit->DOMEnd() && !unit->IsCollapsed();
}

bool OffsetMapping::IsAfterNonCollapsedContent(const InlinePosition& position) const {
  const unsigned offset = position.offset;
  if (!offset)
    return false;
  // In case we have one unit ending at |offset| and another starting at
  // |offset|, we need to find the former. Hence, search with |offset - 1|.
  const OffsetMappingUnit* unit = GetMappingUnitForPosition({position.node, offset - 1});
  return unit && offset > unit->DOMStart() && !unit->IsCollapsed();
}

std::optional<UChar> OffsetMapping::GetCharacterBefore(const InlinePosition& position) const {
  std::optional<unsigned> text_content_offset = GetTextContentOffset(position);
  if (!text_content_offset || !*text_content_offset)
    return std::nullopt;
  return text_[*text_content_offset - 1];
}

InlinePosition OffsetMapping::GetFirstPosition(unsigned offset) const {
  // Find the first unit where |unit.TextContentEnd() >= offset|
  if (units_.empty() || units_.back().TextContentEnd() < offset)
    return {};
  const auto* result = std::lower_bound(units_.data(), units_.data() + units_.size(), offset,
                                        [](const OffsetMappingUnit& unit, unsigned offset) {
                                          return unit.TextContentEnd() < offset;
                                        });
  const InlineObject& node = result->GetOwner();
  const unsigned dom_offset = result->ConvertTextContentToFirstDOMOffset(offset);
  return CreatePositionForOffsetMapping(node, dom_offset);
}

const OffsetMappingUnit* OffsetMapping::GetFirstMappingUnit(unsigned offset) const {
  // Find the first unit where |unit.TextContentEnd() >= offset|.
  if (units_.empty() || units_.front().TextContentStart() > offset)
    return nullptr;
  const auto* const end = units_.data() + units_.size();
  const auto* result = std::lower_bound(units_.data(), end, offset,
                                        [](const OffsetMappingUnit& unit, unsigned offset) {
                                          return unit.TextContentEnd() < offset;
                                        });
  if (result == end)
    return nullptr;
  const auto* next_unit = std::next(result);
  if (next_unit != end && next_unit->TextContentStart() == offset) {
    // For offset=2, returns [1] instead of [0].
    // For offset=3, returns [3] instead of [2],
    // in below example:
    //  text_content = "ab\ncd"
    //  offset mapping unit:
    //   [0] I DOM:0-2 TC:0-2 "ab"
    //   [1] C DOM:2-3 TC:2-2
    //   [2] I DOM:3-4 TC:2-3 "\n"
    //   [3] C DOM:4-5 TC:3-3
    //   [4] I DOM:5-7 TC:3-5 "cd"
    return next_unit;
  }
  return result;
}

const OffsetMappingUnit* OffsetMapping::GetLastMappingUnit(unsigned offset) const {
  // Find the last unit where |unit.TextContentStart() <= offset|
  if (units_.empty() || units_.front().TextContentStart() > offset)
    return nullptr;
  const auto* result = std::upper_bound(units_.data(), units_.data() + units_.size(), offset,
                                        [](unsigned offset, const OffsetMappingUnit& unit) {
                                          return offset < unit.TextContentStart();
                                        });
  result = std::prev(result);
  if (result->TextContentEnd() < offset)
    return nullptr;
  return result;
}

InlinePosition OffsetMapping::GetLastPosition(unsigned offset) const {
  const OffsetMappingUnit* result = GetLastMappingUnit(offset);
  if (!result)
    return {};
  const InlineObject& node = result->GetOwner();
  const unsigned dom_offset = result->ConvertTextContentToLastDOMOffset(offset);
  return CreatePositionForOffsetMapping(node, dom_offset);
}

InlinePosition OffsetMapping::GetPosition(unsigned offset, TextAffinity affinity) const {
  InlinePosition position =
      affinity == TextAffinity::kDownstream ? GetLastPosition(offset) : GetFirstPosition(offset);
  if (position) position.affinity = affinity;
  return position;
}

bool OffsetMapping::HasBidiControlCharactersOnly(unsigned start, unsigned end) const {
  if (start > end || end > text_.length()) return false;
  for (unsigned i = start; i < end; ++i) {
    if (!Character::IsBidiControl(text_[i]))
      return false;
  }
  return true;
}

} // namespace bkfont
