// Ported from: blink/renderer/core/editing/bidi_adjustment.cc
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
#include "bidi_adjustment.h"

#include <cassert>
#include <unicode/ubidi.h>

namespace bkit {
namespace {

// NG-only adapter; the traversal/adjustment algorithms below are upstream's.
class AbstractInlineBox {
public:
  AbstractInlineBox() = default;
  explicit AbstractInlineBox(const InlineCursor& cursor) {
    InlineCursor line = cursor.CursorForRoot();
    line.MoveToContainingLine();
    cursor_ = line.CursorForDescendants();
    cursor_.MoveToItem(cursor.ItemIndex());
  }
  bool IsNotNull() const {
    return static_cast<bool>(cursor_);
  }
  bool IsNull() const {
    return !cursor_;
  }
  InlineCursor GetCursor() const {
    return cursor_.CursorForRoot();
  }
  UBiDiLevel BidiLevel() const {
    return static_cast<UBiDiLevel>(cursor_.Current()->BidiLevel());
  }
  TextDirection Direction() const {
    return cursor_.Current()->ResolvedDirection();
  }
  TextDirection ParagraphDirection() const {
    auto line = GetCursor();
    line.MoveToContainingLine();
    return line.Current()->ResolvedDirection();
  }
  AbstractInlineBox PrevLeafChild() const {
    auto previous = cursor_;
    previous.MoveToPrevious();
    while (previous && previous.Current()->IsGeneratedText()) previous.MoveToPrevious();
    return previous ? AbstractInlineBox(previous) : AbstractInlineBox();
  }
  AbstractInlineBox NextLeafChild() const {
    auto next = cursor_;
    next.MoveToNext();
    while (next && next.Current()->IsGeneratedText()) next.MoveToNext();
    return next ? AbstractInlineBox(next) : AbstractInlineBox();
  }
  AbstractInlineBox PrevLeafChildIgnoringLineBreak() const {
    auto box = PrevLeafChild();
    while (box.IsNotNull() && box.cursor_.Current()->IsLineBreak()) box = box.PrevLeafChild();
    return box;
  }
  AbstractInlineBox NextLeafChildIgnoringLineBreak() const {
    auto box = NextLeafChild();
    while (box.IsNotNull() && box.cursor_.Current()->IsLineBreak()) box = box.NextLeafChild();
    return box;
  }

private:
  InlineCursor cursor_;
};

enum SideAffinity {
  kLeft,
  kRight
};

class AbstractInlineBoxAndSideAffinity {
public:
  AbstractInlineBoxAndSideAffinity(const AbstractInlineBox& box, SideAffinity side)
      : box_(box),
        side_(side) {
  }
  explicit AbstractInlineBoxAndSideAffinity(const InlineCaretPosition& position)
      : box_(position.cursor) {
    const bool at_start = position.text_offset == position.cursor.Current()->TextOffset().start;
    side_ = at_start == IsLtr(box_.Direction()) ? kLeft : kRight;
  }
  InlineCaretPosition ToInlineCaretPosition() const {
    auto cursor = box_.GetCursor();
    const bool at_start = IsLtr(box_.Direction()) == AtLeftSide();
    return {cursor, cursor.Current()->IsText() ? InlineCaretPositionType::kAtTextOffset : at_start ? InlineCaretPositionType::kBeforeBox
                                                                                                   : InlineCaretPositionType::kAfterBox,
            at_start ? cursor.Current()->TextOffset().start : cursor.Current()->TextOffset().end};
  }
  AbstractInlineBox GetBox() const {
    return box_;
  }
  bool AtLeftSide() const {
    return side_ == kLeft;
  }

private:
  AbstractInlineBox box_;
  SideAffinity side_;
};

struct TraverseRight;

// "Left" traversal strategy
struct TraverseLeft {

  using Backwards = TraverseRight;

  static AbstractInlineBox Forward(const AbstractInlineBox& box) {
    return box.PrevLeafChild();
  }

  static AbstractInlineBox ForwardIgnoringLineBreak(
      const AbstractInlineBox& box) {
    return box.PrevLeafChildIgnoringLineBreak();
  }

  static AbstractInlineBox Backward(const AbstractInlineBox& box);
  static AbstractInlineBox BackwardIgnoringLineBreak(
      const AbstractInlineBox& box);

  static SideAffinity ForwardSideAffinity() {
    return SideAffinity::kLeft;
  }
};

// "Left" traversal strategy
struct TraverseRight {

  using Backwards = TraverseLeft;

  static AbstractInlineBox Forward(const AbstractInlineBox& box) {
    return box.NextLeafChild();
  }

  static AbstractInlineBox ForwardIgnoringLineBreak(
      const AbstractInlineBox& box) {
    return box.NextLeafChildIgnoringLineBreak();
  }

  static AbstractInlineBox Backward(const AbstractInlineBox& box) {
    return Backwards::Forward(box);
  }

  static AbstractInlineBox BackwardIgnoringLineBreak(
      const AbstractInlineBox& box) {
    return Backwards::ForwardIgnoringLineBreak(box);
  }

  static SideAffinity ForwardSideAffinity() {
    return SideAffinity::kRight;
  }
};

// static
AbstractInlineBox TraverseLeft::Backward(const AbstractInlineBox& box) {
  return Backwards::Forward(box);
}

// static
AbstractInlineBox TraverseLeft::BackwardIgnoringLineBreak(
    const AbstractInlineBox& box) {
  return Backwards::ForwardIgnoringLineBreak(box);
}

template <typename TraversalStrategy>
using Backwards = typename TraversalStrategy::Backwards;

template <typename TraversalStrategy>
AbstractInlineBoxAndSideAffinity AbstractInlineBoxAndForwardSideAffinity(
    const AbstractInlineBox& box) {
  return AbstractInlineBoxAndSideAffinity(
      box, TraversalStrategy::ForwardSideAffinity());
}

template <typename TraversalStrategy>
AbstractInlineBoxAndSideAffinity AbstractInlineBoxAndBackwardSideAffinity(
    const AbstractInlineBox& box) {
  return AbstractInlineBoxAndForwardSideAffinity<Backwards<TraversalStrategy>>(
      box);
}

// Template algorithms for traversing in bidi runs

// Traverses from |start|, and returns the first box with bidi level less than
// or equal to |bidi_level| (excluding |start| itself). Returns a null box when
// such a box doesn't exist.
template <typename TraversalStrategy>
AbstractInlineBox FindBidiRun(const AbstractInlineBox& start,
                              unsigned bidi_level) {
  assert(start.IsNotNull());
  for (AbstractInlineBox runner = TraversalStrategy::Forward(start);
       runner.IsNotNull(); runner = TraversalStrategy::Forward(runner)) {
    if (runner.BidiLevel() <= bidi_level)
      return runner;
  }
  return AbstractInlineBox();
}

// Traverses from |start|, and returns the last non-linebreak box with bidi
// level greater than |bidi_level| (including |start| itself).
template <typename TraversalStrategy>
AbstractInlineBox FindBoundaryOfBidiRunIgnoringLineBreak(
    const AbstractInlineBox& start,
    unsigned bidi_level) {
  assert(start.IsNotNull());
  AbstractInlineBox last_runner = start;
  for (AbstractInlineBox runner =
           TraversalStrategy::ForwardIgnoringLineBreak(start);
       runner.IsNotNull();
       runner = TraversalStrategy::ForwardIgnoringLineBreak(runner)) {
    if (runner.BidiLevel() <= bidi_level)
      return last_runner;
    last_runner = runner;
  }
  return last_runner;
}

// Traverses from |start|, and returns the last box with bidi level greater than
// or equal to |bidi_level| (including |start| itself). Line break boxes may or
// may not be ignored, depending of the passed |forward| function.
AbstractInlineBox FindBoundaryOfEntireBidiRunInternal(
    const AbstractInlineBox& start,
    unsigned bidi_level,
    AbstractInlineBox (*forward)(const AbstractInlineBox&)) {
  assert(start.IsNotNull());
  AbstractInlineBox last_runner = start;
  for (AbstractInlineBox runner = forward(start); runner.IsNotNull();
       runner = forward(runner)) {
    if (runner.BidiLevel() < bidi_level)
      return last_runner;
    last_runner = runner;
  }
  return last_runner;
}

// Variant of |FindBoundaryOfEntireBidiRun| preserving line break boxes.
template <typename TraversalStrategy>
AbstractInlineBox FindBoundaryOfEntireBidiRun(const AbstractInlineBox& start,
                                              unsigned bidi_level) {
  return FindBoundaryOfEntireBidiRunInternal(start, bidi_level,
                                             TraversalStrategy::Forward);
}

// Variant of |FindBoundaryOfEntireBidiRun| ignoring line break boxes.
template <typename TraversalStrategy>
AbstractInlineBox FindBoundaryOfEntireBidiRunIgnoringLineBreak(
    const AbstractInlineBox& start,
    unsigned bidi_level) {
  return FindBoundaryOfEntireBidiRunInternal(
      start, bidi_level, TraversalStrategy::ForwardIgnoringLineBreak);
}

// Adjustment algorithm at the end of caret position resolution.
template <typename TraversalStrategy>
class InlineCaretPositionResolutionAdjuster {

public:
  static AbstractInlineBoxAndSideAffinity UnadjustedInlineCaretPosition(
      const AbstractInlineBox& box) {
    return AbstractInlineBoxAndBackwardSideAffinity<TraversalStrategy>(box);
  }

  // Returns true if |box| starts different direction of embedded text run.
  // See [1] for details.
  // [1] UNICODE BIDIRECTIONAL ALGORITHM, http://unicode.org/reports/tr9/
  static bool IsStartOfDifferentDirection(const AbstractInlineBox&);

  static AbstractInlineBoxAndSideAffinity AdjustForPrimaryDirectionAlgorithm(
      const AbstractInlineBox& box) {
    if (IsStartOfDifferentDirection(box))
      return UnadjustedInlineCaretPosition(box);

    const unsigned level = TraversalStrategy::Backward(box).BidiLevel();
    const AbstractInlineBox forward_box =
        FindBidiRun<TraversalStrategy>(box, level);

    // For example, abc FED 123 ^ CBA when adjusting right side of 123
    if (forward_box.IsNotNull() && forward_box.BidiLevel() == level)
      return UnadjustedInlineCaretPosition(box);

    // For example, abc 123 ^ CBA when adjusting right side of 123
    const AbstractInlineBox result_box =
        FindBoundaryOfEntireBidiRun<Backwards<TraversalStrategy>>(box, level);
    return AbstractInlineBoxAndBackwardSideAffinity<TraversalStrategy>(
        result_box);
  }

  static AbstractInlineBoxAndSideAffinity AdjustFor(
      const AbstractInlineBox& box) {
    assert(box.IsNotNull());

    const TextDirection primary_direction = box.ParagraphDirection();
    if (box.Direction() == primary_direction)
      return AdjustForPrimaryDirectionAlgorithm(box);

    const unsigned char level = box.BidiLevel();
    const AbstractInlineBox backward_box =
        TraversalStrategy::BackwardIgnoringLineBreak(box);
    if (backward_box.IsNull() || backward_box.BidiLevel() < level) {
      // Backward side of a secondary run. Set to the forward side of the entire
      // run.
      const AbstractInlineBox result_box =
          FindBoundaryOfEntireBidiRunIgnoringLineBreak<TraversalStrategy>(
              box, level);
      return AbstractInlineBoxAndForwardSideAffinity<TraversalStrategy>(
          result_box);
    }

    if (backward_box.BidiLevel() <= level)
      return UnadjustedInlineCaretPosition(box);

    // Forward side of a "tertiary" run. Set to the backward side of that run.
    const AbstractInlineBox result_box =
        FindBoundaryOfBidiRunIgnoringLineBreak<Backwards<TraversalStrategy>>(
            box, level);
    return AbstractInlineBoxAndBackwardSideAffinity<TraversalStrategy>(
        result_box);
  }
};

// TODO(editing-dev): Try to unify the algorithms for both directions.
template <>
bool InlineCaretPositionResolutionAdjuster<
    TraverseLeft>::IsStartOfDifferentDirection(const AbstractInlineBox& box) {
  assert(box.IsNotNull());
  const AbstractInlineBox backward_box = TraverseRight::Forward(box);
  if (backward_box.IsNull())
    return true;
  return backward_box.BidiLevel() >= box.BidiLevel();
}

template <>
bool InlineCaretPositionResolutionAdjuster<
    TraverseRight>::IsStartOfDifferentDirection(const AbstractInlineBox& box) {
  assert(box.IsNotNull());
  const AbstractInlineBox backward_box = TraverseLeft::Forward(box);
  if (backward_box.IsNull())
    return true;
  if (backward_box.Direction() == box.Direction())
    return true;
  return backward_box.BidiLevel() > box.BidiLevel();
}

// Adjustment algorithm at the end of hit tests.
template <typename TraversalStrategy>
class HitTestAdjuster {

public:
  static AbstractInlineBoxAndSideAffinity UnadjustedHitTestPosition(
      const AbstractInlineBox& box) {
    return AbstractInlineBoxAndBackwardSideAffinity<TraversalStrategy>(box);
  }

  static AbstractInlineBoxAndSideAffinity AdjustFor(
      const AbstractInlineBox& box) {
    // TODO(editing-dev): Fix handling of left on 12CBA
    if (box.Direction() == box.ParagraphDirection())
      return UnadjustedHitTestPosition(box);

    const UBiDiLevel level = box.BidiLevel();

    const AbstractInlineBox backward_box =
        TraversalStrategy::BackwardIgnoringLineBreak(box);
    if (backward_box.IsNotNull() && backward_box.BidiLevel() == level)
      return UnadjustedHitTestPosition(box);

    if (backward_box.IsNotNull() && backward_box.BidiLevel() > level) {
      // e.g. left of B in aDC12BAb when adjusting left side
      const AbstractInlineBox backward_most_box =
          FindBoundaryOfBidiRunIgnoringLineBreak<Backwards<TraversalStrategy>>(
              backward_box, level);
      return AbstractInlineBoxAndForwardSideAffinity<TraversalStrategy>(
          backward_most_box);
    }

    // backward_box.IsNull() || backward_box.BidiLevel() < level
    // e.g. left of D in aDC12BAb when adjusting left side
    const AbstractInlineBox forward_most_box =
        FindBoundaryOfEntireBidiRunIgnoringLineBreak<TraversalStrategy>(box,
                                                                        level);
    return box.Direction() == forward_most_box.Direction()
               ? AbstractInlineBoxAndForwardSideAffinity<TraversalStrategy>(
                     forward_most_box)
               : AbstractInlineBoxAndBackwardSideAffinity<TraversalStrategy>(
                     forward_most_box);
  }
};

bool NeedsAdjustment(const InlineCaretPosition& position) {
  if (!position) return false;
  const auto range = position.cursor.Current()->TextOffset();
  return position.text_offset == range.start || position.text_offset == range.end;
}

} // namespace

InlineCaretPosition AdjustCaretForBidi(const InlineCaretPosition& position) {
  if (!NeedsAdjustment(position)) return position;
  const AbstractInlineBoxAndSideAffinity unadjusted(position);
  const auto adjusted = unadjusted.AtLeftSide() ? InlineCaretPositionResolutionAdjuster<TraverseRight>::AdjustFor(unadjusted.GetBox()) : InlineCaretPositionResolutionAdjuster<TraverseLeft>::AdjustFor(unadjusted.GetBox());
  return adjusted.ToInlineCaretPosition();
}

InlineCaretPosition AdjustHitTestForBidi(const InlineCaretPosition& position) {
  if (!NeedsAdjustment(position)) return position;
  const AbstractInlineBoxAndSideAffinity unadjusted(position);
  const auto adjusted = unadjusted.AtLeftSide() ? HitTestAdjuster<TraverseRight>::AdjustFor(unadjusted.GetBox()) : HitTestAdjuster<TraverseLeft>::AdjustFor(unadjusted.GetBox());
  return adjusted.ToInlineCaretPosition();
}

} // namespace bkit
