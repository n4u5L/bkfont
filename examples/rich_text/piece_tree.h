// Example document storage. All tree links and free-list links are arena indices.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rich_text {

class PieceTree {
public:
  using Index = uint32_t;
  using Offset = uint32_t; // UTF-16 code units, matching bkfont editing positions.
  struct Piece {
    uint32_t buffer = 0; // 0: immutable original, 1: append-only additions.
    Offset start = 0;
    Offset length = 0;
    uint32_t style = 0;
    bool operator==(const Piece&) const = default;
  };
  struct Statistics {
    size_t live_nodes;
    size_t free_nodes;
    size_t capacity;
    size_t buffer_units;
  };

  PieceTree();
  void Reset(std::u16string original = {}, std::span<const Piece> pieces = {});
  Offset Length() const;
  Offset LineBreaks() const;
  Offset LineBreaksBefore(Offset) const;
  uint32_t StyleAt(Offset) const;
  std::u16string_view View(const Piece&) const;
  std::u16string Text() const;
  std::vector<Piece> Slice(Offset start, Offset length) const;
  // These descriptors remain valid across node recycling and buffer reallocation;
  // undo records own descriptors, never node indices or character pointers.
  Piece Append(std::u16string_view, uint32_t style);
  void Replace(Offset start, Offset length, std::span<const Piece> replacement);
  Statistics Stats() const;
  bool Validate() const; // Red/black, parent, augmentation and free-list invariants.

private:
  struct Node {
    Piece piece;
    Index parent = 0;
    Index left = 0;
    Index right = 0;
    Index next_free = 0;
    Offset units = 0;
    Offset breaks = 0;
    Offset piece_breaks = 0;
    bool red = false;
    bool live = false;
  };
  struct Location {
    Index node;
    Offset offset;
  };
  Location Locate(Offset) const;
  Index Allocate(Piece);
  void Release(Index);
  void Pull(Index);
  void PullAncestors(Index);
  Index Minimum(Index) const;
  Index Successor(Index) const;
  Index Predecessor(Index) const;
  void RotateLeft(Index);
  void RotateRight(Index);
  void FixInsert(Index);
  void FixErase(Index node, Index parent);
  void Transplant(Index, Index);
  Index InsertBefore(Index next, Piece);
  void Erase(Index);
  Index Boundary(Offset);
  void Coalesce(Offset);
  Offset CountBreaks(const Piece&) const;

  std::u16string buffers_[2];
  // Sorted offsets avoid rescanning a long piece each time it is split.
  std::vector<Offset> line_starts_[2];
  std::vector<Node> nodes_; // Index 0 is the immutable black NIL sentinel.
  Index root_ = 0;
  Index free_head_ = 0;
  size_t live_nodes_ = 0;
};

} // namespace rich_text
