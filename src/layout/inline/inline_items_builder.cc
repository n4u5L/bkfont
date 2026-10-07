// Ported from: blink/renderer/core/layout/inline/inline_items_builder.cc
// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "inline_items_builder.h"

#include <cassert>
#include <span>

#include "base/text/character_names.h"
#include "text/character.h"

namespace bkit {

namespace {

// The preserved whitespace controls handled by Blink's items/line builders.
// CR and FF remain in the mapping, but the line builder ignores them.
bool IsPreservedControl(UChar c) {
  return c == uchar::kLineFeed || c == uchar::kTab ||
         c == uchar::kCarriageReturn || c == uchar::kFormFeed;
}

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
void InlineItemsBuilder::AppendTextItem(const TransformedString& string, const InlineObject& layout_object) {
  AppendTextItem(InlineItem::kText, string, layout_object);
}

InlineItem& InlineItemsBuilder::AppendTextItem(InlineItem::InlineItemType type, const TransformedString& string,
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
  AppendBreakOpportunity(layout_object).is_generated_for_line_break = true;
}

void InlineItemsBuilder::AppendTransformedString(const TransformedString& transformed) {
  text_.Append(transformed.View());
  if (!transformed.HasLengthMap()) {
    mapping_builder_->AppendIdentityMapping(transformed.View().length());
    return;
  }

  // 1 followed by 0+     => expanded
  // 2 or larger          => shrink
  // 1+ not followed by 0 => identity
  unsigned identity_start = kNotFound;
  unsigned size = transformed.View().length();
  for (unsigned i = 0; i < size; ++i) {
    TextOffsetMap::Length len = transformed.LengthMap()[i];
    if (len > 1u) {
      if (identity_start != kNotFound) {
        mapping_builder_->AppendIdentityMapping(i - identity_start);
        identity_start = kNotFound;
      }
      unsigned zero_length = 0;
      for (++i; i < size; ++i) {
        if (transformed.LengthMap()[i] != 0) {
          --i;
          break;
        }
        ++zero_length;
      }
      mapping_builder_->AppendVariableMapping(len, 1u + zero_length);
    } else if (len == 0u) {
      // LengthMap should not start with 0.
      assert(i != 0u);
      // 2+ followed by zeros should be handled in the above block. So we
      // handle only 1, 0, ... here.
      assert(identity_start != kNotFound);
      if (i - identity_start > 1) mapping_builder_->AppendIdentityMapping(i - identity_start - 1);
      identity_start = kNotFound;
      unsigned zero_length = 1;
      for (++i; i < size; ++i) {
        if (transformed.LengthMap()[i] != 0) {
          --i;
          break;
        }
        ++zero_length;
      }
      mapping_builder_->AppendVariableMapping(1u, 1u + zero_length);
    } else {
      assert(len == 1u);
      if (identity_start == kNotFound) identity_start = i;
    }
  }
  if (identity_start != kNotFound) mapping_builder_->AppendIdentityMapping(size - identity_start);
}

void InlineItemsBuilder::AppendText(const InlineObject& layout_object) {
  const String& original = layout_object.Text();
  if (original.empty()) {
    AppendEmptyTextItem(layout_object);
    return;
  }

  // LayoutText::TransformAndSecureText() and
  // GetVariableLengthTransformResult().
  const ComputedStyle& style = layout_object.Style();
  TextOffsetMap offset_map;
  const String transformed_text = style.ApplyTextTransform(original, previous_character_, &offset_map);
  if (transformed_text.empty()) {
    AppendEmptyTextItem(layout_object);
    return;
  }
  previous_character_ = transformed_text[transformed_text.length() - 1];
  Vector<TextOffsetMap::Length> length_map;
  if (!offset_map.IsEmpty()) length_map = offset_map.CreateLengthMap(original.length(), transformed_text.length());
  assert(length_map.empty() || length_map.size() == transformed_text.length());
  const TransformedString string(StringView(transformed_text),
                                 std::span<const TextOffsetMap::Length>(length_map.data(), length_map.size()));

  OffsetMappingBuilder::SourceNodeScope scope(mapping_builder_, &layout_object);

  RestoreTrailingCollapsibleSpaceIfRemoved();

  if (style.ShouldPreserveWhiteSpaces()) {
    AppendPreserveWhitespace(string, style, layout_object);
  } else if (style.ShouldPreserveBreaks()) {
    AppendPreserveNewline(string, style, layout_object);
  } else {
    AppendCollapseWhitespace(string, style, layout_object);
  }
}

void InlineItemsBuilder::AppendCollapseWhitespace(const TransformedString& transformed, const ComputedStyle& style,
                                                  const InlineObject& layout_object) {
  const StringView string = transformed.View();
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
        } else if (!item->object->Style().ShouldWrapLine() && style.ShouldWrapLine()) {
          // A collapsed space retains its break opportunity when the space it
          // collapsed into belongs to nowrap text. Forced breaks already end
          // the line and do not need a generated opportunity.
          if (item->type != InlineItem::kControl || text_[item->start] != uchar::kLineFeed)
            AppendGeneratedBreakOpportunity(layout_object);
        }
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
      AppendTransformedString(transformed.Substring(start_of_non_space, i - start_of_non_space));

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
  if (style.ShouldCollapseWhiteSpaces() || !style.ShouldWrapLine() || !string.length() || index >= string.length() ||
      string[index] != uchar::kSpace) {
    return false;
  }

  // Preserved leading spaces must be at the beginning of the first line or just
  // after a forced break.
  if (index)
    return string[index - 1] == uchar::kLineFeed;
  return text_.empty() || text_[text_.length() - 1] == uchar::kLineFeed;
}

void InlineItemsBuilder::InsertBreakOpportunityAfterLeadingPreservedSpaces(const TransformedString& transformed,
                                                                           const ComputedStyle& style,
                                                                           const InlineObject& layout_object,
                                                                           unsigned* start) {
  const StringView string = transformed.View();
  if (ShouldInsertBreakOpportunityAfterLeadingPreservedSpaces(string, style, *start)) [[unlikely]] {
    wtf_size_t end = *start;
    do {
      ++end;
    } while (end < string.length() && string[end] == uchar::kSpace);
    AppendTextItem(transformed.Substring(*start, end - *start), layout_object);
    AppendGeneratedBreakOpportunity(layout_object);
    *start = end;
  }
}

// Preserved newlines, tabs and ignored CR/FF characters have control items,
// so the line builder can handle them without shaping visible glyphs.
void InlineItemsBuilder::AppendPreserveWhitespace(const TransformedString& transformed, const ComputedStyle& style,
                                                  const InlineObject& layout_object) {
  const StringView string = transformed.View();
  // A soft wrap opportunity exists at the end of the sequence of preserved
  // spaces. https://drafts.csswg.org/css-text-3/#white-space-phase-1
  // Due to our optimization to give opportunities before spaces, the
  // opportunity after leading preserved spaces needs a special code in the line
  // breaker. Generate an opportunity to make it easy.
  unsigned start = 0;
  InsertBreakOpportunityAfterLeadingPreservedSpaces(transformed, style, layout_object, &start);
  const wtf_size_t length = string.length();
  while (start < length) {
    wtf_size_t control = start;
    while (control < length && !IsPreservedControl(string[control])) ++control;
    if (control != start) {
      AppendTextItem(transformed.Substring(start, control - start), layout_object);
      if (control >= length) {
        break;
      }
      start = control;
    }
    if (string[start] != uchar::kLineFeed) {
      Append(InlineItem::kControl, string[start], layout_object);
      ++start;
      continue;
    }
    AppendForcedBreak(layout_object);
    start++;
    // A forced break is not a collapsible space, but following collapsible
    // spaces are leading spaces and they need a special code in the line
    // breaker. Generate an opportunity to make it easy.
    InsertBreakOpportunityAfterLeadingPreservedSpaces(transformed, style, layout_object, &start);
  }
}

void InlineItemsBuilder::AppendPreserveNewline(const TransformedString& transformed, const ComputedStyle& style,
                                               const InlineObject& layout_object) {
  const StringView string = transformed.View();
  for (unsigned start = 0; start < string.length();) {
    if (string[start] == uchar::kLineFeed) {
      AppendForcedBreakCollapseWhitespace(layout_object);
      start++;
      continue;
    }

    unsigned end = start + 1;
    while (end < string.length() && string[end] != uchar::kLineFeed) ++end;
    AppendCollapseWhitespace(transformed.Substring(start, end - start), style, layout_object);
    start = end;
  }
}

void InlineItemsBuilder::AppendForcedBreak(const InlineObject& layout_object) {
  // At the forced break, add bidi controls to pop all contexts.
  // https://drafts.csswg.org/css-writing-modes-3/#bidi-embedding-breaks
  if (!bidi_context_.empty()) {
    OffsetMappingBuilder::SourceNodeScope scope(mapping_builder_, nullptr);
    // These bidi controls need to be associated with the |layout_object| so
    // that items from a LayoutObject are consecutive.
    for (wtf_size_t i = bidi_context_.size(); i-- > 0;)
      AppendOpaque(InlineItem::kBidiControl, bidi_context_[i].exit, &layout_object);
  }

  InlineItem& item = Append(InlineItem::kControl, uchar::kLineFeed, layout_object);

  // A forced break is not a collapsible space, but following collapsible spaces
  // are leading spaces and that they should be collapsed.
  // Pretend that this item ends with a collapsible space, so that following
  // collapsible spaces can be collapsed.
  item.end_collapse_type = InlineItem::kCollapsible;
  item.is_end_collapsible_newline = false;

  // Then re-add bidi controls to restore the bidi context.
  if (!bidi_context_.empty()) {
    OffsetMappingBuilder::SourceNodeScope scope(mapping_builder_, nullptr);
    for (const auto& bidi : bidi_context_) AppendOpaque(InlineItem::kBidiControl, bidi.enter, &layout_object);
  }
}

void InlineItemsBuilder::AppendForcedBreakCollapseWhitespace(const InlineObject& layout_object) {
  // Remove collapsible spaces immediately before a preserved newline.
  RemoveTrailingCollapsibleSpaceIfExists();

  AppendForcedBreak(layout_object);
}

InlineItem& InlineItemsBuilder::AppendBreakOpportunity(const InlineObject& layout_object) {
  return AppendOpaque(InlineItem::kControl, uchar::kZeroWidthSpace, &layout_object);
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
  // An atomic inline ends the PreviousCharacter() search.
  previous_character_ = uchar::kSpace;
  OffsetMappingBuilder::SourceNodeScope scope(mapping_builder_, &layout_object);
  RestoreTrailingCollapsibleSpaceIfRemoved();
  Append(InlineItem::kAtomicInline, uchar::kObjectReplacementCharacter, layout_object);
}

void InlineItemsBuilder::EnterBidiContext(const InlineObject* node, UChar enter, UChar exit) {
  AppendOpaque(InlineItem::kBidiControl, enter, node);
  bidi_context_.push_back(BidiContext{node, enter, exit});
  has_bidi_controls_ = true;
}

void InlineItemsBuilder::EnterBidiContext(const InlineObject* node, const ComputedStyle& style, UChar ltr_enter,
                                          UChar rtl_enter, UChar exit) {
  EnterBidiContext(node, IsLtr(style.Direction()) ? ltr_enter : rtl_enter, exit);
}

void InlineItemsBuilder::EnterBlock(const ComputedStyle& style) {
  // Handle bidi-override on the block itself. 'direction' is the paragraph
  // level (BidiParagraph::SetParagraph()).
  OffsetMappingBuilder::SourceNodeScope scope(mapping_builder_, nullptr);
  switch (style.GetUnicodeBidi()) {
    case UnicodeBidi::kNormal:
    case UnicodeBidi::kEmbed:
    case UnicodeBidi::kIsolate:
      // Isolate and embed values are enforced by default and redundant on the
      // block elements.
      if (style.Direction() == TextDirection::kRtl) has_bidi_controls_ = true;
      break;
    case UnicodeBidi::kBidiOverride:
    case UnicodeBidi::kIsolateOverride:
      EnterBidiContext(nullptr, style, uchar::kLeftToRightOverride, uchar::kRightToLeftOverride,
                       uchar::kPopDirectionalFormatting);
      break;
    case UnicodeBidi::kPlaintext:
      // Plaintext is handled as the paragraph level by
      // BidiParagraph::SetParagraph().
      has_bidi_controls_ = true;
      has_unicode_bidi_plain_text_ = true;
      break;
  }
}

void InlineItemsBuilder::EnterInline(const InlineObject& layout_object) {
  // https://drafts.csswg.org/css-writing-modes-3/#bidi-control-codes-injection-table
  const ComputedStyle& style = layout_object.Style();
  {
    OffsetMappingBuilder::SourceNodeScope scope(mapping_builder_, nullptr);
    switch (style.GetUnicodeBidi()) {
      case UnicodeBidi::kNormal: break;
      case UnicodeBidi::kEmbed:
        EnterBidiContext(&layout_object, style, uchar::kLeftToRightEmbedding, uchar::kRightToLeftEmbedding,
                         uchar::kPopDirectionalFormatting);
        break;
      case UnicodeBidi::kBidiOverride:
        EnterBidiContext(&layout_object, style, uchar::kLeftToRightOverride, uchar::kRightToLeftOverride,
                         uchar::kPopDirectionalFormatting);
        break;
      case UnicodeBidi::kIsolate:
        EnterBidiContext(&layout_object, style, uchar::kLeftToRightIsolate, uchar::kRightToLeftIsolate,
                         uchar::kPopDirectionalIsolate);
        break;
      case UnicodeBidi::kPlaintext:
        has_unicode_bidi_plain_text_ = true;
        EnterBidiContext(&layout_object, uchar::kFirstStrongIsolate, uchar::kPopDirectionalIsolate);
        break;
      case UnicodeBidi::kIsolateOverride:
        EnterBidiContext(&layout_object, uchar::kFirstStrongIsolate, uchar::kPopDirectionalIsolate);
        EnterBidiContext(&layout_object, style, uchar::kLeftToRightOverride, uchar::kRightToLeftOverride,
                         uchar::kPopDirectionalFormatting);
        break;
    }
  }
  items_->push_back(InlineItem{&layout_object, text_.length(), text_.length(),
                               InlineItem::kOpenTag, InlineItem::kOpaqueToCollapsing});
}

void InlineItemsBuilder::ExitInline(const InlineObject& layout_object) {
  items_->push_back(InlineItem{&layout_object, text_.length(), text_.length(),
                               InlineItem::kCloseTag, InlineItem::kOpaqueToCollapsing});
  OffsetMappingBuilder::SourceNodeScope scope(mapping_builder_, nullptr);
  Exit(&layout_object);
}

void InlineItemsBuilder::Exit(const InlineObject* node) {
  while (!bidi_context_.empty() && bidi_context_.back().node == node) {
    AppendOpaque(InlineItem::kBidiControl, bidi_context_.back().exit, node);
    bidi_context_.pop_back();
  }
}

InlineItem& InlineItemsBuilder::AppendOpaque(InlineItem::InlineItemType type, UChar character,
                                             const InlineObject* layout_object) {
  // Controls of the block itself (null node upstream) belong to the root
  // object here, so that every item has an object.
  if (!layout_object) layout_object = block_object_;
  InlineItem& item = Append(type, character, *layout_object);
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
  {
    OffsetMappingBuilder::SourceNodeScope scope(mapping_builder_, nullptr);
    Exit(nullptr);
  }

  // Segment Break Transformation Rules[1] defines to keep trailing new lines,
  // but it will be removed in Phase II[2]. We prefer not to add trailing new
  // lines and collapsible spaces in Phase I.
  RemoveTrailingCollapsibleSpaceIfExists();
}

} // namespace bkit
