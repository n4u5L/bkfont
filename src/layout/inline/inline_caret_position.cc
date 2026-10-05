// Ported from: blink/renderer/core/layout/inline/inline_caret_position.cc
// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "inline_caret_position.h"

#include <algorithm>

#include "base/text/character_names.h"
#include "editing/bidi_adjustment.h"
#include "runtime_enabled_features.h"

namespace bkfont {
namespace {

bool CanResolveBefore(const InlineCursor& cursor, TextAffinity affinity) {
  if (affinity == TextAffinity::kDownstream) return true;
  if (RuntimeEnabledFeatures::BidiCaretAffinityEnabled()) return false;
  auto line = cursor.CursorForRoot();
  line.MoveToContainingLine();
  auto first = line;
  first.MoveToFirstLogicalLeaf();
  if (first.Current() != cursor.Current()) return true;
  line.MoveToPreviousLine();
  return !line || !line.Current()->HasSoftWrapToNextLine();
}

bool CanResolveAfter(const InlineCursor& cursor, TextAffinity affinity) {
  if (affinity == TextAffinity::kUpstream) return true;
  if (RuntimeEnabledFeatures::BidiCaretAffinityEnabled()) return false;
  auto line = cursor.CursorForRoot();
  line.MoveToContainingLine();
  auto last = line;
  last.MoveToLastLogicalLeaf();
  if (last.Current() != cursor.Current()) return true;
  return !line.Current()->HasSoftWrapToNextLine();
}

bool IsUpstreamAfterLineBreak(const InlineCaretPosition& position) {
  return position && position.cursor.Current()->IsLineBreak() &&
         position.text_offset == position.cursor.Current()->TextOffset().end;
}

InlineCaretPosition AdjustInlineCaretPositionForBidiText(const InlineCaretPosition& position) {
  if (RuntimeEnabledFeatures::BidiCaretAffinityEnabled()) return position;
  return AdjustCaretForBidi(position);
}

} // namespace

InlinePosition InlineCaretPosition::ToPositionInDOMTreeWithAffinity(const OffsetMapping& mapping) const {
  if (!*this) return {};
  const FragmentItem& item = *cursor.Current();
  switch (position_type) {
    case InlineCaretPositionType::kBeforeBox:
      return {item.GetLayoutObject()->Id(), 0, TextAffinity::kDownstream};
    case InlineCaretPositionType::kAfterBox:
      return {item.GetLayoutObject()->Id(), 1, TextAffinity::kUpstream};
    case InlineCaretPositionType::kAtTextOffset:
      break;
  }
  const TextAffinity affinity =
      text_offset == item.TextOffset().end ? TextAffinity::kUpstream : TextAffinity::kDownstream;
  return mapping.GetPosition(text_offset, affinity);
}

InlineCaretPosition ComputeInlineCaretPosition(InlineFormattingContext& context, InlinePosition position) {
  const auto& fragments = context.Fragments();
  const auto mapped_offset = fragments.Mapping().GetTextContentOffset(position);
  if (!mapped_offset) return {};
  // Blink resolves upstream before ZWS so the two affinities remain distinct
  // when a line breaks before a generated break opportunity.
  const unsigned offset = position.affinity == TextAffinity::kUpstream && *mapped_offset &&
                                  fragments.TextContent()[*mapped_offset - 1] == uchar::kZeroWidthSpace
                              ? *mapped_offset - 1 : *mapped_offset;
  const auto* preferred = context.Find(position.node);
  if (preferred && !preferred->IsText()) preferred = nullptr;
  InlineCaretPosition candidate;
  InlineCursor cursor(context);
  if (preferred && preferred->HasInlineFragments()) cursor.MoveTo(*preferred);
  for (; cursor; cursor.MoveToNext()) {
    const FragmentItem& item = *cursor.Current();
    if (item.IsGeneratedText() || item.Type() == FragmentItem::kLine) continue;
    const auto range = item.TextOffset();
    if (item.IsAtomicInline()) {
      // TryResolveInlineCaretPositionByBoxFragmentSide accepts only the two
      // box boundaries; skipping adjacent bidi controls is text-only.
      if (offset != range.start && offset != range.end) continue;
    } else {
      if (offset < range.start && !fragments.Mapping().HasBidiControlCharactersOnly(offset, range.start)) continue;
      if (offset > range.end && !fragments.Mapping().HasBidiControlCharactersOnly(range.end, offset)) continue;
    }
    const unsigned clamped = std::clamp(offset, range.start, range.end);
    const InlineCaretPosition current{cursor, item.IsAtomicInline() ? (clamped == range.start ? InlineCaretPositionType::kBeforeBox : InlineCaretPositionType::kAfterBox) : InlineCaretPositionType::kAtTextOffset, clamped};
    const bool resolved = (clamped > range.start && clamped < range.end) ||
                          (clamped == range.start && CanResolveBefore(cursor, position.affinity)) ||
                          (clamped == range.end && !item.IsLineBreak() && CanResolveAfter(cursor, position.affinity));
    if (resolved) {
      candidate = current;
      if (!preferred || item.GetLayoutObject() == preferred) return AdjustInlineCaretPositionForBidiText(current);
      continue;
    }
    if (!candidate || IsUpstreamAfterLineBreak(candidate)) candidate = current;
  }
  return AdjustInlineCaretPositionForBidiText(candidate);
}

} // namespace bkfont
