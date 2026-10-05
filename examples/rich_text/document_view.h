#pragma once

#include <memory>
#include <vector>

#include "document.h"
#include "inline_layout.h"

namespace bkfont {

class RasterCanvas;

} // namespace bkfont

namespace rich_text {

// The PieceTree is authoritative. InlineObjects are disposable layout adapters;
// only their stable IDs are kept when mapping back to document offsets.
class DocumentView {
public:
  struct NodeMap {
    bkfont::InlineNodeId id;
    uint32_t start, length;
  };
  struct Block {
    uint32_t start = 0, end = 0, style = 0;
    float x = 0, y = 0, width = 0, height = 0;
    bool vertical = false;
    std::vector<PieceTree::Piece> pieces;
    std::vector<NodeMap> nodes;
    std::unique_ptr<bkfont::InlineFormattingContext> context;
    bkfont::InlinePosition Position(uint32_t offset, bkfont::TextAffinity = bkfont::TextAffinity::kDownstream) const;
  };
  void Update(const Document&, float device_scale, float zoom, bool force = false);
  uint32_t Hit(float x, float y, bkfont::TextAffinity* = nullptr);
  bkfont::PhysicalRect Caret(uint32_t, bkfont::TextAffinity = bkfont::TextAffinity::kDownstream);
  void Paint(bkfont::RasterCanvas&, float x, float y, float clip_top, float clip_bottom, const Document::Selection&);
  uint32_t Move(uint32_t offset, int direction, bool word) const;
  uint32_t Snap(uint32_t offset) const;
  const Block& BlockAt(uint32_t offset) const;
  const std::u16string& Text() const {
    return text_;
  }
  float Width() const {
    return page_width_;
  }
  float Height() const {
    return page_height_;
  }

private:
  std::vector<Block> blocks_;
  std::vector<uint32_t> boundaries_;
  std::u16string text_;
  float scale_ = 0;
  float page_width_ = 0, page_height_ = 0;
  uint64_t revision_ = ~uint64_t{0};
};

} // namespace rich_text
