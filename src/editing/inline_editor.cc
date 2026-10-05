// Local editing commands. Grapheme and geometry calculations use the same
// Blink/ICU shaping data as painting, including bidi and soft-wrap affinity.
#include "inline_editor.h"

#include <algorithm>

#include "layout/inline/inline_caret_position.h"
#include "text/character_break_iterator.h"

namespace bkfont {

InlineEditor::InlineEditor(InlineFormattingContext& context)
    : context_(context) {
  selection_.anchor = selection_.focus = {context.RootObject().Id(), 0};
}

void InlineEditor::NormalizeSelection() {
  if (line_move_zoom_factor_ != context_.LayoutZoomFactor()) {
    line_move_position_.reset();
    line_move_zoom_factor_ = context_.LayoutZoomFactor();
  }
  const auto& mapping = context_.Fragments().Mapping();
  if (!mapping.GetTextContentOffset(selection_.focus)) selection_.focus = {context_.RootObject().Id(), 0};
  if (!mapping.GetTextContentOffset(selection_.anchor)) selection_.anchor = selection_.focus;
}

InlineSelection InlineEditor::Selection() {
  NormalizeSelection();
  return selection_;
}

bool InlineEditor::SetSelection(InlineSelection selection) {
  const auto& mapping = context_.Fragments().Mapping();
  if (!mapping.GetTextContentOffset(selection.anchor) || !mapping.GetTextContentOffset(selection.focus)) return false;
  CommitComposition();
  selection_ = selection;
  typing_style_.reset();
  typing_node_ = 0;
  line_move_position_.reset();
  return true;
}

void InlineEditor::SelectAll() {
  CommitComposition();
  typing_style_.reset();
  typing_node_ = 0;
  selection_ = {{context_.RootObject().Id(), 0},
                {context_.RootObject().Id(), static_cast<unsigned>(context_.RootObject().Children().size())}};
  line_move_position_.reset();
}

void InlineEditor::SelectAt(const PhysicalOffset& point, bool extend) {
  CommitComposition();
  NormalizeSelection();
  selection_.focus = context_.HitTest(point);
  typing_style_.reset();
  typing_node_ = 0;
  if (!extend) selection_.anchor = selection_.focus;
  line_move_position_.reset();
}

InlineEditor::NodeState InlineEditor::SaveNode(const InlineObject& object) {
  NodeState state{object.Id(), object.GetType(), object.Text(), object.SpecifiedStyle(), object.AtomicSize(), object.AtomicBaseline(), {}};
  for (const auto& child : object.Children()) state.children.push_back(SaveNode(*child));
  return state;
}

InlineEditor::State InlineEditor::Save() {
  NormalizeSelection();
  return {SaveNode(context_.RootObject()), selection_, typing_style_, typing_node_};
}

std::unique_ptr<InlineObject> InlineEditor::RestoreNode(const NodeState& state, InlineObject* parent) {
  auto object = std::unique_ptr<InlineObject>(new InlineObject(context_, state.id, state.type, state.style));
  object->parent_ = parent;
  object->text_ = state.text;
  object->atomic_size_ = state.size;
  object->atomic_baseline_ = state.baseline;
  context_.objects_.emplace(object->Id(), object.get());
  for (const auto& child : state.children) object->children_.push_back(RestoreNode(child, object.get()));
  return object;
}

void InlineEditor::Restore(const State& state) {
  context_.MarkDirty(*context_.root_);
  for (auto& child : context_.root_->children_) {
    context_.Retire(*child);
    context_.retired_.push_back(std::move(child));
  }
  context_.root_->children_.clear();
  context_.root_->style_ = state.root.style;
  for (const auto& child : state.root.children)
    context_.root_->children_.push_back(RestoreNode(child, context_.root_.get()));
  selection_ = state.selection;
  typing_style_ = state.typing_style;
  typing_node_ = state.typing_node;
  context_.UpdateLayout();
  line_move_position_.reset();
}

void InlineEditor::BeginCommand() {
  CommitComposition();
  undo_.push_back(Save());
  redo_.clear();
  line_move_position_.reset();
}

InlinePosition InlineEditor::TextPositionAt(unsigned offset, InlinePosition preferred) {
  const auto& mapping = context_.Fragments().Mapping();
  const InlineObject* object = context_.Find(preferred.node);
  if (object && object->IsText()) {
    const auto current = mapping.GetTextContentOffset(preferred);
    if (current && *current == offset) return preferred;
  }
  InlinePosition position = mapping.GetPosition(offset);
  object = context_.Find(position.node);
  if (object && object->IsText()) return position;
  const InlineObject& parent = object && object->Parent() ? *object->Parent() : context_.RootObject();
  const InlineObject* before = object ? (position.offset == 0 ? object : object->NextSibling()) : nullptr;
  const auto& text = context_.AppendText(parent, String());
  context_.Move(text, parent, before);
  return {text.Id(), 0};
}

void InlineEditor::ReplaceSelection(const String& text) {
  NormalizeSelection();
  const auto& mapping = context_.Fragments().Mapping();
  const unsigned anchor = *mapping.GetTextContentOffset(selection_.anchor);
  const unsigned focus = *mapping.GetTextContentOffset(selection_.focus);
  const unsigned start = std::min(anchor, focus);
  const unsigned end = std::max(anchor, focus);
  InlinePosition insertion = anchor <= focus ? selection_.anchor : selection_.focus;
  // The copy contains live model pointers; retired objects stay allocated
  // until UpdateLayout, which is deliberately after this mutation batch.
  const auto units = mapping.Units();
  for (const auto& unit : units) {
    const unsigned from = std::max(start, unit.start);
    const unsigned to = std::min(end, unit.end);
    if (from >= to) continue;
    if (unit.object->IsText())
      context_.ReplaceText(*unit.object, from - unit.start, to - from, String());
    else
      context_.Remove(*unit.object);
  }
  insertion = TextPositionAt(start, insertion);
  const auto* object = context_.Find(insertion.node);
  if (!text.empty() && typing_style_ && insertion.node != typing_node_) {
    const String suffix = object->Text().Substring(insertion.offset);
    const auto original_style = object->SpecifiedStyle();
    const InlineObject& parent = *object->Parent();
    const InlineObject* before = object->NextSibling();
    context_.ReplaceText(*object, insertion.offset, suffix.length(), String());
    const auto& typed = context_.AppendText(parent, String(), typing_style_);
    context_.Move(typed, parent, before);
    if (!suffix.empty()) {
      const auto& after = context_.AppendText(parent, suffix, original_style);
      context_.Move(after, parent, before);
    }
    insertion = {typed.Id(), 0};
    object = &typed;
    typing_node_ = typed.Id();
  }
  context_.ReplaceText(*object, insertion.offset, 0, text);
  insertion.offset += text.length();
  insertion.affinity = TextAffinity::kDownstream;
  selection_ = {insertion, insertion};
  context_.UpdateLayout();
}

void InlineEditor::InsertText(const String& text) {
  NormalizeSelection();
  const auto& mapping = context_.Fragments().Mapping();
  if (text.empty() && mapping.GetTextContentOffset(selection_.anchor) == mapping.GetTextContentOffset(selection_.focus)) return;
  BeginCommand();
  ReplaceSelection(text);
}

void InlineEditor::DeleteBackward() {
  BeginCommand();
  const auto& mapping = context_.Fragments().Mapping();
  const unsigned anchor = *mapping.GetTextContentOffset(selection_.anchor);
  const unsigned focus = *mapping.GetTextContentOffset(selection_.focus);
  if (anchor == focus && focus) {
    CharacterBreakIterator breaks{StringView(mapping.GetText())};
    const int previous = breaks.Preceding(static_cast<int>(focus));
    selection_.anchor = mapping.GetPosition(previous == kTextBreakDone ? 0 : static_cast<unsigned>(previous));
  }
  ReplaceSelection(String());
}

void InlineEditor::DeleteForward() {
  BeginCommand();
  const auto& mapping = context_.Fragments().Mapping();
  const unsigned anchor = *mapping.GetTextContentOffset(selection_.anchor);
  const unsigned focus = *mapping.GetTextContentOffset(selection_.focus);
  if (anchor == focus && focus < mapping.GetText().length()) {
    CharacterBreakIterator breaks{StringView(mapping.GetText())};
    const int next = breaks.Following(static_cast<int>(focus));
    selection_.focus = mapping.GetPosition(next == kTextBreakDone ? mapping.GetText().length() : static_cast<unsigned>(next));
  }
  ReplaceSelection(String());
}

String InlineEditor::SelectedText() {
  NormalizeSelection();
  const auto& mapping = context_.Fragments().Mapping();
  const unsigned a = *mapping.GetTextContentOffset(selection_.anchor);
  const unsigned b = *mapping.GetTextContentOffset(selection_.focus);
  return mapping.GetText().Substring(std::min(a, b), std::max(a, b) - std::min(a, b));
}

PhysicalRect InlineEditor::CaretRect() {
  NormalizeSelection();
  return context_.CaretRect(selection_.focus);
}
Vector<PhysicalRect> InlineEditor::SelectionRects() {
  NormalizeSelection();
  return context_.SelectionRects(selection_);
}

void InlineEditor::ApplyStyle(const InlineStyle& style) {
  CommitComposition();
  NormalizeSelection();
  const auto& mapping = context_.Fragments().Mapping();
  const unsigned anchor = *mapping.GetTextContentOffset(selection_.anchor);
  const unsigned focus = *mapping.GetTextContentOffset(selection_.focus);
  const unsigned start = std::min(anchor, focus), end = std::max(anchor, focus);
  if (start == end) {
    typing_style_ = std::make_shared<const InlineStyle>(style);
    typing_node_ = 0;
    return;
  }
  BeginCommand();
  typing_style_.reset();
  typing_node_ = 0;
  const auto units = mapping.Units();
  for (const auto& unit : units) {
    if (!unit.object->IsText()) continue;
    const unsigned from = std::max(start, unit.start), to = std::min(end, unit.end);
    if (from >= to) continue;
    const InlineObject& object = *unit.object;
    if (from == unit.start && to == unit.end) {
      context_.SetStyle(object, style);
      continue;
    }
    const String original = object.Text();
    const auto original_style = object.SpecifiedStyle();
    const InlineObject& parent = *object.Parent();
    const InlineObject* before = object.NextSibling();
    context_.ReplaceText(object, from - unit.start, unit.end - from, String());
    const auto& middle = context_.AppendText(parent, original.Substring(from - unit.start, to - from),
                                             std::make_shared<const InlineStyle>(style));
    context_.Move(middle, parent, before);
    if (to < unit.end) {
      const auto& suffix = context_.AppendText(parent, original.Substring(to - unit.start), original_style);
      context_.Move(suffix, parent, before);
    }
  }
  const auto& updated = context_.Fragments().Mapping();
  selection_.anchor = updated.GetPosition(anchor, selection_.anchor.affinity);
  selection_.focus = updated.GetPosition(focus, selection_.focus.affinity);
}

void InlineEditor::SetComposition(const String& text, unsigned selection_start, unsigned selection_end) {
  if (selection_start > text.length() || selection_end > text.length()) return;
  if (!before_composition_) {
    before_composition_ = Save();
    redo_.clear();
  } else if (composition_) {
    selection_ = *composition_;
  }
  NormalizeSelection();
  const auto& mapping = context_.Fragments().Mapping();
  const unsigned start = std::min(*mapping.GetTextContentOffset(selection_.anchor),
                                  *mapping.GetTextContentOffset(selection_.focus));
  ReplaceSelection(text);
  const auto& updated = context_.Fragments().Mapping();
  composition_ = InlineSelection{updated.GetPosition(start), updated.GetPosition(start + text.length(), TextAffinity::kUpstream)};
  selection_ = {updated.GetPosition(start + selection_start), updated.GetPosition(start + selection_end)};
}

void InlineEditor::CommitComposition() {
  if (before_composition_) undo_.push_back(std::move(*before_composition_));
  before_composition_.reset();
  composition_.reset();
}

void InlineEditor::CancelComposition() {
  if (before_composition_) Restore(*before_composition_);
  before_composition_.reset();
  composition_.reset();
}

bool InlineEditor::Undo() {
  CommitComposition();
  if (undo_.empty()) return false;
  redo_.push_back(Save());
  State state = std::move(undo_.back());
  undo_.pop_back();
  Restore(state);
  return true;
}

bool InlineEditor::Redo() {
  CommitComposition();
  if (redo_.empty()) return false;
  undo_.push_back(Save());
  State state = std::move(redo_.back());
  redo_.pop_back();
  Restore(state);
  return true;
}

void InlineEditor::MoveCaret(InlineCaretMove move, bool extend) {
  CommitComposition();
  NormalizeSelection();
  typing_style_.reset();
  typing_node_ = 0;
  const auto& fragments = context_.Fragments();
  const auto& mapping = fragments.Mapping();
  const unsigned focus = *mapping.GetTextContentOffset(selection_.focus);
  const unsigned anchor = *mapping.GetTextContentOffset(selection_.anchor);
  const bool visual = move == InlineCaretMove::kVisualForward || move == InlineCaretMove::kVisualBackward;
  const bool character = visual || move == InlineCaretMove::kForward || move == InlineCaretMove::kBackward;
  bool forward = move == InlineCaretMove::kForward || move == InlineCaretMove::kVisualForward;
  if (visual) {
    // SelectionModifier::ModifyMovingRight/Left uses the line's base
    // direction. Extending uses the enclosing block's direction. Neither
    // searches painted caret coordinates, which can coincide after snapping.
    TextDirection direction = context_.Options().direction;
    if (!extend) {
      const auto focus_caret = ComputeInlineCaretPosition(context_, selection_.focus);
      if (focus != anchor) {
        // DirectionOfSelection(): prefer a common direction at both ends,
        // otherwise use the enclosing block's direction.
        const auto anchor_caret = ComputeInlineCaretPosition(context_, selection_.anchor);
        if (focus_caret && anchor_caret &&
            focus_caret.cursor.Current()->ResolvedDirection() == anchor_caret.cursor.Current()->ResolvedDirection())
          direction = focus_caret.cursor.Current()->ResolvedDirection();
      } else if (focus_caret) {
        direction = fragments[focus_caret.cursor.Current()->LineIndex()].ResolvedDirection();
      }
    }
    forward = (move == InlineCaretMove::kVisualForward) == IsLtr(direction);
  }
  if (!extend && focus != anchor && character) {
    selection_.anchor = selection_.focus = mapping.GetPosition(forward ? std::max(anchor, focus) : std::min(anchor, focus));
    line_move_position_.reset();
    return;
  }
  unsigned target = focus;
  if (character) {
    CharacterBreakIterator breaks{StringView(mapping.GetText())};
    const int next = forward ? breaks.Following(static_cast<int>(focus)) : breaks.Preceding(static_cast<int>(focus));
    if (next != kTextBreakDone) target = static_cast<unsigned>(next);
    selection_.focus = mapping.GetPosition(target, forward ? TextAffinity::kDownstream : TextAffinity::kUpstream);
  } else if (move == InlineCaretMove::kStart || move == InlineCaretMove::kEnd) {
    selection_.focus = mapping.GetPosition(move == InlineCaretMove::kStart ? 0 : mapping.GetText().length());
  } else {
    auto caret = ComputeInlineCaretPosition(context_, selection_.focus);
    if (!caret) return;
    const size_t line_index = caret.cursor.Current()->LineIndex();
    const auto& line = fragments[line_index];
    const auto lines = fragments.Lines();
    const size_t line_number = std::lower_bound(lines.begin(), lines.end(), line_index) - lines.begin();
    if (move == InlineCaretMove::kLineStart || move == InlineCaretMove::kLineEnd) {
      unsigned offset = move == InlineCaretMove::kLineStart ? line.TextOffset().start : line.TextOffset().end;
      if (move == InlineCaretMove::kLineEnd && offset > line.TextOffset().start) {
        const UChar last = mapping.GetText()[offset - 1];
        if (last == '\n') {
          --offset;
          if (offset > line.TextOffset().start && mapping.GetText()[offset - 1] == '\r') --offset;
        } else if (last == '\r' || last == 0x2028) {
          --offset;
        }
      }
      selection_.focus = mapping.GetPosition(offset, move == InlineCaretMove::kLineStart ? TextAffinity::kDownstream : TextAffinity::kUpstream);
    } else {
      const bool horizontal = IsHorizontalWritingMode(context_.Options().writing_mode);
      const auto rect = context_.CaretRect(selection_.focus);
      const LayoutUnit current = horizontal ? rect.X() : rect.Y();
      if (move == InlineCaretMove::kNextLine || move == InlineCaretMove::kPreviousLine) {
        if (!line_move_position_) line_move_position_ = current;
        const size_t next = move == InlineCaretMove::kNextLine ? std::min(line_number + 1, lines.size() - 1) : line_number ? line_number - 1
                                                                                                                           : 0;
        const auto& next_rect = fragments[lines[next]].RectInContainerFragment();
        selection_.focus = context_.HitTest(horizontal ? PhysicalOffset(*line_move_position_, next_rect.Y() + next_rect.Height() / 2) : PhysicalOffset(next_rect.X() + next_rect.Width() / 2, *line_move_position_));
      }
    }
  }
  if (!selection_.focus) selection_.focus = {context_.RootObject().Id(), 0};
  if (!extend) selection_.anchor = selection_.focus;
  if (move != InlineCaretMove::kNextLine && move != InlineCaretMove::kPreviousLine) line_move_position_.reset();
}

} // namespace bkfont
