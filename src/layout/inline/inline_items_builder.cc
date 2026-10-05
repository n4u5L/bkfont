// Ported from: blink/renderer/core/layout/inline/inline_items_builder.cc
// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "inline_items_builder.h"

#include "base/text/character_names.h"
#include "text/character.h"

namespace bkfont {

namespace {

// Determine whether a newline should be removed or not.
// CSS Text, Segment Break Transformation Rules
// https://drafts.csswg.org/css-text-3/#line-break-transform
// SEGMENT_BREAK_TRANSFORMATION_FOR_EAST_ASIAN_WIDTH is 0 upstream, so only the
// zero width space rule applies and the styles are not consulted.
bool ShouldRemoveNewlineSlow(const StringBuilder& before, unsigned space_index, const StringView& after) {
  // Remove if either before/after the newline is zeroWidthSpaceCharacter.
  UChar32 last = 0;
  if (space_index) {
    last = before[space_index - 1];
    if (last == uchar::kZeroWidthSpace) {
      return true;
    }
  }
  UChar32 next = 0;
  if (!after.empty()) {
    next = after[0];
    if (next == uchar::kZeroWidthSpace) {
      return true;
    }
  }
  return false;
}

bool ShouldRemoveNewline(const StringBuilder& before, unsigned space_index, const StringView& after) {
  // All characters before/after removable newline are 16 bits.
  return (!before.Is8Bit() || !after.Is8Bit()) && ShouldRemoveNewlineSlow(before, space_index, after);
}

// Find the end of the collapsible spaces.
// Returns whether this space run contains a newline or not, because it changes
// the collapsing behavior.
inline bool MoveToEndOfCollapsibleSpaces(const StringView& string, unsigned* offset, UChar* c) {
  bool space_run_has_newline = *c == uchar::kLineFeed;
  for ((*offset)++; *offset < string.length(); (*offset)++) {
    *c = string[*offset];
    space_run_has_newline |= *c == uchar::kLineFeed;
    if (!Character::IsCollapsibleSpace(*c))
      break;
  }
  return space_run_has_newline;
}

} // namespace

// Find the last item to compute collapsing with. Opaque items such as
// open/close or bidi controls are ignored.
// Returns nullptr if there were no previous items.
InlineItem* InlineItemsBuilder::LastItemToCollapseWith() {
  for (wtf_size_t i = items_->size(); i;) {
    InlineItem& item = (*items_)[--i];
    if (item.end_collapse_type != InlineItem::kOpaqueToCollapsing) {
      return &item;
    }
  }
  return nullptr;
}

// Append a string as a text item.
void InlineItemsBuilder::AppendTextItem(StringView string, const InlineObject& layout_object) {
  AppendTextItem(InlineItem::kText, string, layout_object);
}

InlineItem& InlineItemsBuilder::AppendTextItem(InlineItem::InlineItemType type, StringView string,
                                               const InlineObject& layout_object) {
  unsigned start_offset = text_.length();
  AppendTransformedString(string);
  items_->push_back(InlineItem{&layout_object, start_offset, text_.length(), type});
  return items_->back();
}

// Empty text items are not needed for the layout purposes, but all objects
// are captured in the items to maintain their states.
void InlineItemsBuilder::AppendEmptyTextItem(const InlineObject& layout_object) {
  unsigned offset = text_.length();
  items_->push_back(InlineItem{&layout_object, offset, offset, InlineItem::kText,
                               InlineItem::kOpaqueToCollapsing});
}

// Same as AppendBreakOpportunity, but the generated character has no source
// object in the offset mapping.
void InlineItemsBuilder::AppendGeneratedBreakOpportunity(const InlineObject& layout_object) {
  OffsetMappingBuilder::SourceNodeScope scope(mapping_builder_, nullptr);
  AppendBreakOpportunity(layout_object);
}

void InlineItemsBuilder::AppendTransformedString(StringView string) {
  text_.Append(string);
  mapping_builder_->AppendIdentityMapping(string.length());
}

void InlineItemsBuilder::AppendText(const InlineObject& layout_object) {
  const StringView string(layout_object.Text());
  if (string.empty()) {
    AppendEmptyTextItem(layout_object);
    return;
  }

  OffsetMappingBuilder::SourceNodeScope scope(mapping_builder_, &layout_object);

  const ComputedStyle& style = layout_object.Style();

  RestoreTrailingCollapsibleSpaceIfRemoved();

  if (style.ShouldPreserveWhiteSpaces()) {
    AppendPreserveWhitespace(string, style, layout_object);
  } else if (style.ShouldPreserveBreaks()) {
    AppendPreserveNewline(string, style, layout_object);
  } else {
    AppendCollapseWhitespace(string, style, layout_object);
  }
}

void InlineItemsBuilder::AppendCollapseWhitespace(StringView string, const ComputedStyle& style,
                                                  const InlineObject& layout_object) {
  // This algorithm segments the input string at the collapsible space, and
  // process collapsible space run and non-space run alternately.

  // The first run, regardless it is a collapsible space run or not, is special
  // that it can interact with the last item. Depends on the end of the last
  // item, it may either change collapsing behavior to collapse the leading
  // spaces of this item entirely, or remove the trailing spaces of the last
  // item.

  // Due to this difference, this algorithm process the first run first, then
  // loop through the rest of runs.

  unsigned start_offset;
  InlineItem::CollapseType end_collapse = InlineItem::kNotCollapsible;
  unsigned i = 0;
  UChar c = string[i];
  bool space_run_has_newline = false;
  if (Character::IsCollapsibleSpace(c)) {
    // Find the end of the collapsible space run.
    space_run_has_newline = MoveToEndOfCollapsibleSpaces(string, &i, &c);

    // Check the last item this space run may be collapsed with.
    bool insert_space;
    if (InlineItem* item = LastItemToCollapseWith()) {
      if (item->end_collapse_type == InlineItem::kNotCollapsible) {
        // The last item does not end with a collapsible space.
        // Insert a space to represent this space run.
        insert_space = true;
      } else {
        // The last item ends with a collapsible space this run should collapse
        // to. Collapse the entire space run in this item.
        insert_space = false;

        // If the space run either in this item or in the last item contains a
        // newline, apply segment break rules. This may result in removal of
        // the space in the last item.
        if ((space_run_has_newline || item->is_end_collapsible_newline) && item->type == InlineItem::kText &&
            ShouldRemoveNewline(text_, item->end - 1, StringView(string, i))) {
          RemoveTrailingCollapsibleSpace(item);
          space_run_has_newline = false;
        }
        // Upstream generates a break opportunity when the last item is
        // 'nowrap' and this one is 'wrap'. Wrapping is IFC-wide here, so the
        // two items never differ.
      }
    } else {
      // This space is at the beginning of the paragraph. Remove leading spaces
      // as CSS requires.
      insert_space = false;
    }

    // If this space run contains a newline, apply segment break rules.
    if (space_run_has_newline && ShouldRemoveNewline(text_, text_.length(), StringView(string, i))) {
      insert_space = space_run_has_newline = false;
    }

    // Done computing the interaction with the last item. Start appending.
    start_offset = text_.length();

    unsigned collapsed_length = i;
    if (insert_space) {
      text_.Append(uchar::kSpace);
      mapping_builder_->AppendIdentityMapping(1);
      collapsed_length--;
    }
    if (collapsed_length)
      mapping_builder_->AppendCollapsedMapping(collapsed_length);

    // If this space run is at the end of this item, keep whether the
    // collapsible space run has a newline or not in the item.
    if (i == string.length()) {
      end_collapse = InlineItem::kCollapsible;
    }
  } else {
    // If the last item ended with a collapsible space run with segment breaks,
    // apply segment break rules. This may result in removal of the space in the
    // last item.
    if (InlineItem* item = LastItemToCollapseWith()) {
      if (item->end_collapse_type == InlineItem::kCollapsible && item->is_end_collapsible_newline &&
          ShouldRemoveNewline(text_, item->end - 1, string)) {
        RemoveTrailingCollapsibleSpace(item);
      }
    }

    start_offset = text_.length();
  }

  // The first run is done. Loop through the rest of runs.
  if (i < string.length()) {
    while (true) {
      // Append the non-space text until we find a collapsible space.
      // |string[i]| is guaranteed not to be a space.
      unsigned start_of_non_space = i;
      for (i++; i < string.length(); i++) {
        c = string[i];
        if (Character::IsCollapsibleSpace(c))
          break;
      }
      AppendTransformedString(StringView(string, start_of_non_space, i - start_of_non_space));

      if (i == string.length()) {
        end_collapse = InlineItem::kNotCollapsible;
        break;
      }

      // Process a collapsible space run. First, find the end of the run.
      unsigned start_of_spaces = i;
      space_run_has_newline = MoveToEndOfCollapsibleSpaces(string, &i, &c);

      // Because leading spaces are handled before this loop, no need to check
      // cross-item collapsing.

      // If this space run contains a newline, apply segment break rules.
      bool remove_newline =
          space_run_has_newline && ShouldRemoveNewline(text_, text_.length(), StringView(string, i));
      if (remove_newline) [[unlikely]] {
        // |kNotCollapsible| because the newline is removed, not collapsed.
        end_collapse = InlineItem::kNotCollapsible;
        space_run_has_newline = false;
      } else {
        // If the segment break rules did not remove the run, append a space.
        text_.Append(uchar::kSpace);
        mapping_builder_->AppendIdentityMapping(1);
        start_of_spaces++;
        end_collapse = InlineItem::kCollapsible;
      }

      if (i != start_of_spaces)
        mapping_builder_->AppendCollapsedMapping(i - start_of_spaces);

      // If this space run is at the end of this item, keep whether the
      // collapsible space run has a newline or not in the item.
      if (i == string.length()) {
        break;
      }
    }
  }

  if (text_.length() == start_offset) [[unlikely]] {
    AppendEmptyTextItem(layout_object);
    return;
  }

  items_->push_back(InlineItem{&layout_object, start_offset, text_.length(), InlineItem::kText, end_collapse,
                               space_run_has_newline});
}

bool InlineItemsBuilder::ShouldInsertBreakOpportunityAfterLeadingPreservedSpaces(StringView string,
                                                                                 const ComputedStyle& style,
                                                                                 unsigned index) const {
  // Check if we are at a preserved space character and auto-wrap is enabled.
  if (style.ShouldCollapseWhiteSpaces() || !auto_wrap_ || !string.length() || index >= string.length() ||
      string[index] != uchar::kSpace) {
    return false;
  }

  // Preserved leading spaces must be at the beginning of the first line or just
  // after a forced break.
  if (index)
    return string[index - 1] == uchar::kLineFeed;
  return text_.empty() || text_[text_.length() - 1] == uchar::kLineFeed;
}

void InlineItemsBuilder::InsertBreakOpportunityAfterLeadingPreservedSpaces(StringView string,
                                                                           const ComputedStyle& style,
                                                                           const InlineObject& layout_object,
                                                                           unsigned* start) {
  if (ShouldInsertBreakOpportunityAfterLeadingPreservedSpaces(string, style, *start)) [[unlikely]] {
    wtf_size_t end = *start;
    do {
      ++end;
    } while (end < string.length() && string[end] == uchar::kSpace);
    AppendTextItem(StringView(string, *start, end - *start), layout_object);
    AppendGeneratedBreakOpportunity(layout_object);
    *start = end;
  }
}

// Even when without whitespace collapsing, forced breaks are in their own
// control items. Tabs and other controls stay in text items; the local
// segmentation splits them into their own runs.
void InlineItemsBuilder::AppendPreserveWhitespace(StringView string, const ComputedStyle& style,
                                                  const InlineObject& layout_object) {
  // A soft wrap opportunity exists at the end of the sequence of preserved
  // spaces. https://drafts.csswg.org/css-text-3/#white-space-phase-1
  // Due to our optimization to give opportunities before spaces, the
  // opportunity after leading preserved spaces needs a special code in the line
  // breaker. Generate an opportunity to make it easy.
  unsigned start = 0;
  InsertBreakOpportunityAfterLeadingPreservedSpaces(string, style, layout_object, &start);
  const wtf_size_t length = string.length();
  while (start < length) {
    wtf_size_t control = start;
    while (control < length && string[control] != uchar::kLineFeed) ++control;
    if (control != start) {
      AppendTextItem(StringView(string, start, control - start), layout_object);
      if (control >= length) {
        break;
      }
      start = control;
    }
    AppendForcedBreak(layout_object);
    start++;
    // A forced break is not a collapsible space, but following collapsible
    // spaces are leading spaces and they need a special code in the line
    // breaker. Generate an opportunity to make it easy.
    InsertBreakOpportunityAfterLeadingPreservedSpaces(string, style, layout_object, &start);
  }
}

void InlineItemsBuilder::AppendPreserveNewline(StringView string, const ComputedStyle& style,
                                               const InlineObject& layout_object) {
  for (unsigned start = 0; start < string.length();) {
    if (string[start] == uchar::kLineFeed) {
      AppendForcedBreakCollapseWhitespace(layout_object);
      start++;
      continue;
    }

    unsigned end = start + 1;
    while (end < string.length() && string[end] != uchar::kLineFeed) ++end;
    AppendCollapseWhitespace(StringView(string, start, end - start), style, layout_object);
    start = end;
  }
}

void InlineItemsBuilder::AppendForcedBreak(const InlineObject& layout_object) {
  InlineItem& item = Append(InlineItem::kControl, uchar::kLineFeed, layout_object);

  // A forced break is not a collapsible space, but following collapsible spaces
  // are leading spaces and that they should be collapsed.
  // Pretend that this item ends with a collapsible space, so that following
  // collapsible spaces can be collapsed.
  item.end_collapse_type = InlineItem::kCollapsible;
  item.is_end_collapsible_newline = false;
}

void InlineItemsBuilder::AppendForcedBreakCollapseWhitespace(const InlineObject& layout_object) {
  // Remove collapsible spaces immediately before a preserved newline.
  RemoveTrailingCollapsibleSpaceIfExists();

  AppendForcedBreak(layout_object);
}

InlineItem& InlineItemsBuilder::AppendBreakOpportunity(const InlineObject& layout_object) {
  return AppendOpaque(InlineItem::kControl, uchar::kZeroWidthSpace, layout_object);
}

InlineItem& InlineItemsBuilder::Append(InlineItem::InlineItemType type, UChar character,
                                       const InlineObject& layout_object) {
  text_.Append(character);
  mapping_builder_->AppendIdentityMapping(1);
  unsigned end_offset = text_.length();
  items_->push_back(InlineItem{&layout_object, end_offset - 1, end_offset, type});
  return items_->back();
}

void InlineItemsBuilder::AppendAtomicInline(const InlineObject& layout_object) {
  OffsetMappingBuilder::SourceNodeScope scope(mapping_builder_, &layout_object);
  RestoreTrailingCollapsibleSpaceIfRemoved();
  Append(InlineItem::kAtomicInline, uchar::kObjectReplacementCharacter, layout_object);
}

InlineItem& InlineItemsBuilder::AppendOpaque(InlineItem::InlineItemType type, UChar character,
                                             const InlineObject& layout_object) {
  InlineItem& item = Append(type, character, layout_object);
  item.end_collapse_type = InlineItem::kOpaqueToCollapsing;
  return item;
}

// Removes the collapsible space at the end of |text_| if exists.
void InlineItemsBuilder::RemoveTrailingCollapsibleSpaceIfExists() {
  if (InlineItem* item = LastItemToCollapseWith()) {
    if (item->end_collapse_type == InlineItem::kCollapsible) {
      RemoveTrailingCollapsibleSpace(item);
    }
  }
}

// Removes the collapsible space at the end of the specified item.
void InlineItemsBuilder::RemoveTrailingCollapsibleSpace(InlineItem* item) {
  // A forced break pretends that it's a collapsible space, see
  // |AppendForcedBreak()|. It should not be removed.
  if (item->type != InlineItem::kText) {
    return;
  }

  unsigned space_offset = item->end - 1;
  text_.erase(space_offset);
  mapping_builder_->CollapseTrailingSpace(space_offset);

  // Keep the item even if the length became zero. This is not needed for
  // the layout purposes, but needed to maintain object states. See
  // |AppendEmptyTextItem()|.
  item->end--;
  item->end_collapse_type = InlineItem::kCollapsed;

  // Trailing spaces can be removed across non-character items.
  // Adjust their offsets if after the removed index.
  for (wtf_size_t i = static_cast<wtf_size_t>(item - items_->data()) + 1; i < items_->size(); ++i) {
    (*items_)[i].start--;
    (*items_)[i].end--;
  }
}

// Restore removed collapsible space at the end of items.
void InlineItemsBuilder::RestoreTrailingCollapsibleSpaceIfRemoved() {
  if (InlineItem* last_item = LastItemToCollapseWith()) {
    if (last_item->end_collapse_type == InlineItem::kCollapsed) {
      RestoreTrailingCollapsibleSpace(last_item);
    }
  }
}

// Restore removed collapsible space at the end of the specified item.
void InlineItemsBuilder::RestoreTrailingCollapsibleSpace(InlineItem* item) {
  mapping_builder_->RestoreTrailingCollapsibleSpace(*item->object, item->end);

  // TODO(kojii): Implement StringBuilder::insert().
  if (text_.length() == item->end) {
    text_.Append(uchar::kSpace);
  } else {
    String current = text_.ToString();
    text_.Clear();
    text_.Append(StringView(current, 0, item->end));
    text_.Append(uchar::kSpace);
    text_.Append(StringView(current, item->end));
  }

  item->end++;
  item->end_collapse_type = InlineItem::kCollapsible;

  for (wtf_size_t i = static_cast<wtf_size_t>(item - items_->data()) + 1; i < items_->size(); ++i) {
    (*items_)[i].start++;
    (*items_)[i].end++;
  }
}

void InlineItemsBuilder::ExitBlock() {
  // Segment Break Transformation Rules[1] defines to keep trailing new lines,
  // but it will be removed in Phase II[2]. We prefer not to add trailing new
  // lines and collapsible spaces in Phase I.
  RemoveTrailingCollapsibleSpaceIfExists();
}

} // namespace bkfont
