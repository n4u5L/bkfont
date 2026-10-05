// Local editing commands over the standalone IFC model and Blink geometry.
#pragma once

#include <optional>

#include "base/heap_vector.h"
#include "base/vector.h"
#include "layout/inline/inline_formatting_context.h"

namespace bkfont {

enum class InlineCaretMove {
  kBackward,
  kForward,
  // Blink's left/right character commands: line direction for movement,
  // block direction for extension, and selection direction for collapsing.
  kVisualBackward,
  kVisualForward,
  kPreviousLine,
  kNextLine,
  kLineStart,
  kLineEnd,
  kStart,
  kEnd
};

// The context must outlive its editor. OS input/clipboard/IME adapters call
// these commands; no platform messages or fragment pointers are persisted.
// Undo restores node IDs, but borrowed InlineObject references must be reacquired
// with context.Find(id). Host-side mutations should be followed by SetSelection.
class InlineEditor {
public:
  explicit InlineEditor(InlineFormattingContext&);
  InlineSelection Selection();
  bool SetSelection(InlineSelection);
  void SelectAll();
  void SelectAt(const PhysicalOffset&, bool extend = false);
  void MoveCaret(InlineCaretMove, bool extend = false);
  void InsertText(const String&);
  void DeleteBackward();
  void DeleteForward();
  void ApplyStyle(const InlineStyle&);
  String SelectedText();
  PhysicalRect CaretRect();
  Vector<PhysicalRect> SelectionRects();

  // Composition replaces the current selection. Updates are one undo unit;
  // cancellation restores the pre-composition model and selection.
  void SetComposition(const String&, unsigned selection_start, unsigned selection_end);
  void CommitComposition();
  void CancelComposition();
  std::optional<InlineSelection> Composition() const {
    return composition_;
  }
  bool Undo();
  bool Redo();

private:
  struct NodeState {
    InlineNodeId id;
    InlineObject::Type type;
    String text;
    std::shared_ptr<const InlineStyle> style;
    LogicalSize size;
    LayoutUnit baseline;
    HeapVector<NodeState> children;
  };
  struct State {
    NodeState root;
    InlineSelection selection;
    std::shared_ptr<const InlineStyle> typing_style;
    InlineNodeId typing_node;
  };
  static NodeState SaveNode(const InlineObject&);
  State Save();
  void Restore(const State&);
  std::unique_ptr<InlineObject> RestoreNode(const NodeState&, InlineObject*);
  void BeginCommand();
  void NormalizeSelection();
  void ReplaceSelection(const String&);
  InlinePosition TextPositionAt(unsigned, InlinePosition preferred);

  InlineFormattingContext& context_;
  InlineSelection selection_;
  std::optional<InlineSelection> composition_;
  std::optional<State> before_composition_;
  HeapVector<State> undo_;
  HeapVector<State> redo_;
  std::optional<LayoutUnit> line_move_position_;
  float line_move_zoom_factor_ = 1;
  std::shared_ptr<const InlineStyle> typing_style_;
  InlineNodeId typing_node_ = 0;
};

} // namespace bkfont
