// Ported from: blink/renderer/core/layout/inline/offset_mapping_builder.h
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#pragma once

#include <memory>

#include "base/heap_vector.h"
#include "base/text/wtf_string.h"
#include "base/vector.h"
#include "layout/inline/offset_mapping.h"

namespace bkfont {

// This is the helper class for constructing the model-to-TextContent offset
// mapping. It holds an offset mapping, and provides APIs to modify the mapping
// step by step until the construction is finished.
class OffsetMappingBuilder {
public:
  // A scope-like object that, mappings appended inside the scope are marked as
  // from the given source object. When multiple scopes nest, only the
  // inner-most scope is effective. Note that at most one of the nested scopes
  // may have a non-null object.
  class SourceNodeScope {
  public:
    SourceNodeScope(OffsetMappingBuilder* builder, const InlineObject* node);
    SourceNodeScope(const SourceNodeScope&) = delete;
    SourceNodeScope& operator=(const SourceNodeScope&) = delete;
    ~SourceNodeScope();

  private:
    OffsetMappingBuilder* const builder_ = nullptr;
    // base::AutoReset of current_layout_object_ and current_offset_.
    const InlineObject* const saved_layout_object_;
    const unsigned saved_offset_;
  };

  OffsetMappingBuilder() = default;
  OffsetMappingBuilder(const OffsetMappingBuilder&) = delete;
  OffsetMappingBuilder& operator=(const OffsetMappingBuilder&) = delete;

  void ReserveCapacity(unsigned capacity);

  // Append an identity offset mapping of the specified length with null
  // annotation to the builder.
  void AppendIdentityMapping(unsigned length);

  // Cancel the last AppendIdentityMapping(1) call.
  // This works only for kOpenRubyColumn.
  void RevertIdentityMapping1();

  // Append a collapsed offset mapping from the specified length with null
  // annotation to the builder.
  void AppendCollapsedMapping(unsigned length);

  // Append a variable offset mapping from the specified `dom_length` to the
  // specified `text_content_length`.
  // Either of `dom_length` or `text_content_length` should be 1.
  void AppendVariableMapping(unsigned dom_length, unsigned text_content_length);

  // This function should only be called by the items builder during
  // whitespace collapsing, and in the case that the target string of the
  // currently held mapping:
  // (i)  has at least |space_offset + 1| characters,
  // (ii) character at |space_offset| in destination string is a collapsible
  //      whitespace,
  // This function changes the space into collapsed.
  void CollapseTrailingSpace(unsigned space_offset);

  // Restore a trailing collapsible space at |offset| of text content. The space
  // is associated with |layout_text|.
  void RestoreTrailingCollapsibleSpace(const InlineObject& layout_text, unsigned offset);

  // Set the destination string of the offset mapping.
  // Returns false if the specified string is inconsistent with
  // `destination_length_`. We can't build an OffstMapping in such case.
  bool SetDestinationString(const String&);

  // Finalize and return the offset mapping.
  // This method can only be called once, as it can invalidate the stored data.
  // Local: |root| is walked to derive OffsetMapping::BoundaryMap from the
  // units, after all collapsing has settled.
  std::shared_ptr<const OffsetMapping> Build(const InlineObject& root);

private:
  unsigned AddBoundaries(const InlineObject&, unsigned* offset);

  const InlineObject* current_layout_object_ = nullptr;
  unsigned current_offset_ = 0;
  bool has_open_unit_ = false;

  // Length of the current destination string.
  unsigned destination_length_ = 0;

  // Mapping units of the current mapping function.
  HeapVector<OffsetMappingUnit> mapping_units_;

  // Unit ranges of the current mapping function.
  OffsetMapping::RangeMap unit_ranges_;

  OffsetMapping::BoundaryMap boundaries_;

  // The destination string of the offset mapping.
  String destination_string_;

  friend class SourceNodeScope;
};

} // namespace bkfont
