#pragma once

#include <memory>
#include <string>
#include <utility>

#include "base/vector.h"
#include "document.h"
#include "inline_layout.h"

namespace bkfont {

class RasterCanvas;

} // namespace bkfont

namespace rich_text {

// The PieceTree is authoritative. InlineObjects are disposable layout adapters;
// only their stable IDs are kept when mapping back to document offsets.
// Paragraphs follow the document's edits: an edit rebuilds the paragraphs it
// touched and moves the rest, without rescanning the text.
class DocumentView {
public:
  struct NodeMap {
    bkfont::InlineNodeId id;
    uint32_t start, length;
  };
  struct Block {
    uint32_t start = 0, end = 0, style = 0;
    float x = 0, y = 0, width = 0, height = 0;
    bool vertical = false, rtl = false;
    std::u16string text;                  // Without the newline.
    bkfont::Vector<uint32_t> boundaries;  // Grapheme boundaries in `text`, 0 to text.size().
    size_t words = 0;
    bkfont::Vector<NodeMap> nodes;
    std::unique_ptr<bkfont::InlineFormattingContext> context;
    bkfont::InlinePosition Position(uint32_t offset, bkfont::TextAffinity = bkfont::TextAffinity::kDownstream) const;
  };
  // Page rows (device pixels from the top of the page) whose pixels may differ
  // from what was painted: rebuilt, moved or removed paragraphs, the page end
  // and selection highlight changes. `full` invalidates the whole page.
  struct Rows {
    float top, bottom;
  };
  struct Damage {
    bool full = false;
    bkfont::Vector<Rows> rows;
  };
  void Update(const Document&, float device_scale, float zoom, bool force = false);
  // Records the highlight that the next Paint() will show.
  void InvalidateSelection(const Document::Selection&);
  Damage TakeDamage();
  uint32_t Hit(float x, float y, bkfont::TextAffinity* = nullptr);
  bkfont::PhysicalRect Caret(uint32_t, bkfont::TextAffinity = bkfont::TextAffinity::kDownstream);
  // `marks` paints Word-style paragraph marks (¶) after each paragraph.
  void Paint(bkfont::RasterCanvas&, float x, float y, float clip_top, float clip_bottom, const Document::Selection&,
             bool marks = false);
  uint32_t Move(uint32_t offset, int direction, bool word) const;
  // The UAX #29 word (or run of spaces/punctuation) containing `offset`.
  std::pair<uint32_t, uint32_t> WordAt(uint32_t offset) const;
  uint32_t Snap(uint32_t offset) const;
  const Block& BlockAt(uint32_t offset) const;
  // Word's count: every Han/kana character is a word; other words are runs
  // of letters and digits. A word never spans paragraphs.
  size_t Words() const {
    return words_;
  }
  size_t Words(uint32_t start, uint32_t end) const;
  float Width() const {
    return page_width_;
  }
  float Height() const {
    return page_height_;
  }

private:
  void Build(Block&, const Document&, bkfont::wtf_size_t paragraph, uint32_t start, float device_scale, float zoom);
  bkfont::wtf_size_t BlockIndex(uint32_t offset) const;
  bool IsSpace(uint32_t offset) const;
  void DamageRows(float top, float bottom);
  void DamageRange(uint32_t start, uint32_t end);

  bkfont::Vector<Block> blocks_;
  size_t words_ = 0;
  float scale_ = 0;
  float page_width_ = 0, page_height_ = 0;
  uint64_t version_ = 0; // Document::EditVersion() of blocks_.
  Document::Selection painted_selection_;
  bkfont::Vector<Rows> damage_;
  bool damage_full_ = true;
};

} // namespace rich_text
