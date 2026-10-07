#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>

#include "base/vector.h"
#include "piece_tree.h"
#include "style_catalog.h"

namespace rich_text {

class Document {
public:
  using Offset = PieceTree::Offset;
  struct Selection {
    Offset anchor = 0;
    Offset focus = 0;
    Offset Start() const {
      return std::min(anchor, focus);
    }
    Offset End() const {
      return std::max(anchor, focus);
    }
    bool Empty() const {
      return anchor == focus;
    }
    bool operator==(const Selection&) const = default;
  };
  struct Style {
    Properties properties;
    bkfont::StyleDeclaration declaration;
  };
  // Paragraphs [paragraph, paragraph + removed) became [paragraph,
  // paragraph + inserted); text or style changed in the new ones only.
  struct Edit {
    Offset paragraph = 0, removed = 0, inserted = 0;
  };

  Document();
  void New(bool sample);
  const PieceTree& Tree() const {
    return tree_;
  }
  const bkfont::Vector<uint32_t>& Paragraphs() const {
    return paragraphs_;
  }
  const Style& GetStyle(uint32_t index) const {
    return styles_[index];
  }
  Selection GetSelection() const {
    return selection_;
  }
  void Select(Offset anchor, Offset focus);
  uint32_t TypingStyle() const {
    return typing_style_;
  }
  bool ReplaceSelection(std::u16string_view text, std::string& error);
  bool Format(std::string_view name, std::string_view value, std::string& error);
  bool SetCharacterStyle(Properties, std::string& error);
  void ClearCharacterFormatting();
  bool Undo();
  bool Redo();
  bool CanUndo() const {
    return history_cursor_ != 0;
  }
  bool CanRedo() const {
    return history_cursor_ != history_.size();
  }
  bool Modified() const {
    return revision_ != saved_revision_;
  }
  void MarkSaved() {
    saved_revision_ = revision_;
  }
  uint64_t Revision() const {
    return revision_;
  }
  // Views follow the document through its edits instead of rescanning it.
  // EditVersion() advances with every edit; EditsSince() gives the edits after
  // `version`, oldest first, or false when they are not all recorded (another
  // document, or too old) and everything must be rebuilt.
  uint64_t EditVersion() const {
    return edit_version_;
  }
  bool EditsSince(uint64_t version, std::span<const Edit>& edits) const;
  std::string SelectedText() const;
  std::string Serialize() const;
  bool Load(std::string_view json, std::string& error);
  bool OpenFile(const std::string& path, std::string& error);
  bool SaveFile(const std::string& path, std::string& error);
  bool Validate() const;

private:
  struct Change {
    Offset position = 0;
    bkfont::Vector<PieceTree::Piece> before, after;
    // Only the styles of the edited paragraphs, not of the whole document.
    Edit edit;
    bkfont::Vector<uint32_t> paragraphs_before, paragraphs_after;
    Selection selection_before, selection_after;
    uint64_t revision_before = 0, revision_after = 0;
  };
  uint32_t Intern(Properties);
  Change BeginChange(Offset position, Offset length, Offset paragraph, Offset paragraphs) const;
  void Commit(Change, Offset paragraphs);
  bkfont::Vector<uint32_t> ParagraphStyles(Offset first, Offset count) const;
  void RecordEdit(Edit);
  void Restore(const Change&, bool forward);
  static Offset PiecesLength(const bkfont::Vector<PieceTree::Piece>&);

  PieceTree tree_;
  bkfont::Vector<Style> styles_;
  bkfont::Vector<uint32_t> paragraphs_{0};
  Selection selection_;
  uint32_t typing_style_ = 0;
  bkfont::Vector<Change> history_;
  bkfont::wtf_size_t history_cursor_ = 0;
  uint64_t revision_ = 0, saved_revision_ = 0, next_revision_ = 1;
  bkfont::Vector<Edit> edits_; // The latest edits, ending at edit_version_.
  uint64_t edit_version_;
};

} // namespace rich_text
