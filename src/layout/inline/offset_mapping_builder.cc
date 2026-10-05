// Ported from: blink/renderer/core/layout/inline/offset_mapping_builder.cc
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "offset_mapping_builder.h"

#include <iterator>
#include <utility>

#include "base/notreached.h"

namespace bkfont {

// GetAssociatedStartOffset() is always 0: there is no ::first-letter split.
OffsetMappingBuilder::SourceNodeScope::SourceNodeScope(OffsetMappingBuilder* builder, const InlineObject* node)
    : builder_(builder),
      saved_layout_object_(builder->current_layout_object_),
      saved_offset_(builder->current_offset_) {
  builder_->current_layout_object_ = node;
  builder_->current_offset_ = 0;
  builder_->has_open_unit_ = false;
}

OffsetMappingBuilder::SourceNodeScope::~SourceNodeScope() {
  builder_->has_open_unit_ = false;
  builder_->current_layout_object_ = saved_layout_object_;
  builder_->current_offset_ = saved_offset_;
}

void OffsetMappingBuilder::ReserveCapacity(unsigned capacity) {
  unit_ranges_.ReserveCapacityForSize(capacity);
  mapping_units_.reserve(static_cast<wtf_size_t>(capacity * 1.5));
}

void OffsetMappingBuilder::AppendIdentityMapping(unsigned length) {
  const unsigned dom_start = current_offset_;
  const unsigned dom_end = dom_start + length;
  const unsigned text_content_start = destination_length_;
  const unsigned text_content_end = text_content_start + length;
  current_offset_ += length;
  destination_length_ += length;

  if (!current_layout_object_)
    return;

  if (has_open_unit_ && mapping_units_.back().GetType() == OffsetMappingUnitType::kIdentity) {
    mapping_units_.back().dom_end_ += length;
    mapping_units_.back().text_content_end_ += length;
    return;
  }

  mapping_units_.emplace_back(OffsetMappingUnitType::kIdentity, *current_layout_object_, dom_start, dom_end,
                              text_content_start, text_content_end);
  has_open_unit_ = true;
}

void OffsetMappingBuilder::RevertIdentityMapping1() {
  --current_offset_;
  --destination_length_;
}

void OffsetMappingBuilder::AppendCollapsedMapping(unsigned length) {
  const unsigned dom_start = current_offset_;
  const unsigned dom_end = dom_start + length;
  const unsigned text_content_start = destination_length_;
  const unsigned text_content_end = text_content_start;
  current_offset_ += length;

  if (!current_layout_object_)
    return;

  if (has_open_unit_ && mapping_units_.back().IsCollapsed()) {
    mapping_units_.back().dom_end_ += length;
    return;
  }

  mapping_units_.emplace_back(OffsetMappingUnitType::kCollapsed, *current_layout_object_, dom_start, dom_end,
                              text_content_start, text_content_end);
  has_open_unit_ = true;
}

void OffsetMappingBuilder::AppendVariableMapping(unsigned dom_length, unsigned text_content_length) {
  const unsigned dom_start = current_offset_;
  const unsigned dom_end = dom_start + dom_length;
  const unsigned text_content_start = destination_length_;
  const unsigned text_content_end = text_content_start + text_content_length;
  current_offset_ += dom_length;
  destination_length_ += text_content_length;

  if (!current_layout_object_) {
    return;
  }

  // Don't handle has_open_unit_ here. We can't merge kVariable units.

  mapping_units_.emplace_back(OffsetMappingUnitType::kVariable, *current_layout_object_, dom_start, dom_end,
                              text_content_start, text_content_end);
  has_open_unit_ = false;
}

void OffsetMappingBuilder::CollapseTrailingSpace(unsigned space_offset) {
  --destination_length_;

  OffsetMappingUnit* container_unit = nullptr;
  for (unsigned i = mapping_units_.size(); i;) {
    OffsetMappingUnit& unit = mapping_units_[--i];
    if (unit.TextContentStart() > space_offset) {
      --unit.text_content_start_;
      --unit.text_content_end_;
      continue;
    }
    container_unit = &unit;
    break;
  }

  if (!container_unit || container_unit->TextContentEnd() <= space_offset)
    return;

  // container_unit->TextContentStart()
  // <= space_offset <
  // container_unit->TextContentEnd()
  const InlineObject& layout_object = container_unit->GetLayoutObject();
  unsigned dom_offset = container_unit->DOMStart();
  unsigned text_content_offset = container_unit->TextContentStart();
  unsigned offset_to_collapse = space_offset - text_content_offset;

  HeapVector<OffsetMappingUnit, 3> new_units;
  if (offset_to_collapse) {
    new_units.emplace_back(OffsetMappingUnitType::kIdentity, layout_object, dom_offset,
                           dom_offset + offset_to_collapse, text_content_offset,
                           text_content_offset + offset_to_collapse);
    dom_offset += offset_to_collapse;
    text_content_offset += offset_to_collapse;
  }
  new_units.emplace_back(OffsetMappingUnitType::kCollapsed, layout_object, dom_offset, dom_offset + 1,
                         text_content_offset, text_content_offset);
  ++dom_offset;
  if (dom_offset < container_unit->DOMEnd()) {
    new_units.emplace_back(OffsetMappingUnitType::kIdentity, layout_object, dom_offset, container_unit->DOMEnd(),
                           text_content_offset, container_unit->TextContentEnd() - 1);
  }

  // TODO(xiaochengh): Optimize if this becomes performance bottleneck.
  wtf_size_t position = static_cast<wtf_size_t>(std::distance(mapping_units_.data(), container_unit));
  mapping_units_.EraseAt(position);
  mapping_units_.InsertVector(position, new_units);
  wtf_size_t new_unit_end = position + new_units.size();
  while (new_unit_end && new_unit_end < mapping_units_.size() &&
         mapping_units_[new_unit_end - 1].Concatenate(mapping_units_[new_unit_end])) {
    mapping_units_.EraseAt(new_unit_end);
  }
  while (position && position < mapping_units_.size() &&
         mapping_units_[position - 1].Concatenate(mapping_units_[position])) {
    mapping_units_.EraseAt(position);
  }
}

void OffsetMappingBuilder::RestoreTrailingCollapsibleSpace(const InlineObject& layout_text, unsigned offset) {
  ++destination_length_;
  for (wtf_size_t i = mapping_units_.size(); i;) {
    OffsetMappingUnit& unit = mapping_units_[--i];
    if (unit.text_content_end_ < offset) {
      // There are no collapsed unit.
      NOTREACHED();
    }
    if (unit.text_content_start_ != offset || unit.text_content_end_ != offset ||
        unit.layout_object_ != &layout_text) {
      ++unit.text_content_start_;
      ++unit.text_content_end_;
      continue;
    }
    const unsigned original_dom_end = unit.dom_end_;
    unit.type_ = OffsetMappingUnitType::kIdentity;
    unit.dom_end_ = unit.dom_start_ + 1;
    unit.text_content_end_ = unit.text_content_start_ + 1;
    if (original_dom_end - unit.dom_start_ == 1)
      return;
    // When we collapsed multiple spaces, e.g. <b>   </b>.
    const OffsetMappingUnit collapsed(OffsetMappingUnitType::kCollapsed, layout_text, unit.dom_end_, original_dom_end,
                                      unit.text_content_end_, unit.text_content_end_);
    mapping_units_.insert(i + 1, collapsed);
    return;
  }
  NOTREACHED();
}

// Returns the text content offset at the start of |object| and advances
// |offset| to its end. Objects with units use them; empty text objects and
// inline containers record the offsets of their model positions.
unsigned OffsetMappingBuilder::AddBoundaries(const InlineObject& object, unsigned* offset) {
  if (!object.IsInline()) {
    if (unit_ranges_.IsValidKey(object.Id())) {
      const auto it = unit_ranges_.find(object.Id());
      if (it != unit_ranges_.end()) {
        const unsigned start = mapping_units_[it->value.first].TextContentStart();
        *offset = mapping_units_[it->value.second - 1].TextContentEnd();
        return start;
      }
    }
    boundaries_.insert(object.Id(), Vector<unsigned>{*offset});
    return *offset;
  }
  Vector<unsigned> offsets;
  offsets.ReserveInitialCapacity(object.Children().size() + 1);
  for (const auto& child : object.Children()) offsets.push_back(AddBoundaries(*child, offset));
  offsets.push_back(*offset);
  const unsigned start = offsets.front();
  boundaries_.insert(object.Id(), std::move(offsets));
  return start;
}

bool OffsetMappingBuilder::SetDestinationString(const String& string) {
  if (destination_length_ != string.length()) {
    // If we continue building an OffsetMapping with the inconsistent IFC text
    // content, it might cause out-of-bounds accesses. It happens only if we
    // have a bug, and we should fail safely.
    return false;
  }
  destination_string_ = string;
  return true;
}

std::shared_ptr<const OffsetMapping> OffsetMappingBuilder::Build(const InlineObject& root) {
  // All mapping units are already built. Scan them to build mapping ranges.
  for (unsigned range_start = 0; range_start < mapping_units_.size();) {
    unsigned range_end = range_start + 1;
    const InlineObject* node = mapping_units_[range_start].AssociatedNode();
    while (range_end < mapping_units_.size() && mapping_units_[range_end].AssociatedNode() == node)
      ++range_end;
    // Units of the same object should be consecutive in the mapping function,
    // If not, the layout structure should be already broken.
    unit_ranges_.insert(node->Id(), std::make_pair(range_start, range_end));
    range_start = range_end;
  }
  unsigned offset = 0;
  AddBoundaries(root, &offset);

  return std::make_shared<const OffsetMapping>(std::move(mapping_units_), std::move(unit_ranges_),
                                               destination_string_, std::move(boundaries_));
}

} // namespace bkfont
