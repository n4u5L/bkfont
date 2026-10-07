#include "piece_tree.h"

#include <algorithm>
#include <cassert>
#include <functional>
#include <limits>

namespace rich_text {

PieceTree::PieceTree() {
  Reset();
}

void PieceTree::Reset(std::u16string original, std::span<const Piece> pieces) {
  buffers_[0] = std::move(original);
  buffers_[1].clear();
  line_starts_[0].clear();
  line_starts_[1].clear();
  for (Offset i = 0; i < buffers_[0].size(); ++i)
    if (buffers_[0][i] == u'\n') line_starts_[0].push_back(i);
  nodes_.clear();
  nodes_.reserve(256);
  nodes_.emplace_back();
  root_ = free_head_ = 0;
  live_nodes_ = 0;
  if (pieces.empty() && !buffers_[0].empty())
    InsertBefore(0, {0, 0, static_cast<Offset>(buffers_[0].size()), 0});
  else
    for (const Piece& piece : pieces)
      if (piece.length) InsertBefore(0, piece);
}

PieceTree::Offset PieceTree::Length() const {
  return nodes_[root_].units;
}
PieceTree::Offset PieceTree::LineBreaks() const {
  return nodes_[root_].breaks;
}

std::u16string_view PieceTree::View(const Piece& piece) const {
  return std::u16string_view(buffers_[piece.buffer]).substr(piece.start, piece.length);
}

PieceTree::Offset PieceTree::CountBreaks(const Piece& piece) const {
  const auto& lines = line_starts_[piece.buffer];
  return static_cast<Offset>(std::lower_bound(lines.begin(), lines.end(), piece.start + piece.length) -
                             std::lower_bound(lines.begin(), lines.end(), piece.start));
}

PieceTree::Piece PieceTree::Append(std::u16string_view text, uint32_t style) {
  assert(text.size() <= std::numeric_limits<Offset>::max() - buffers_[1].size());
  const Offset start = static_cast<Offset>(buffers_[1].size());
  for (Offset i = 0; i < text.size(); ++i)
    if (text[i] == u'\n') line_starts_[1].push_back(start + i);
  buffers_[1].append(text);
  return {1, start, static_cast<Offset>(text.size()), style};
}

PieceTree::Location PieceTree::Locate(Offset offset) const {
  Index node = root_;
  while (node) {
    const Offset left = nodes_[nodes_[node].left].units;
    if (offset < left)
      node = nodes_[node].left;
    else if (offset < left + nodes_[node].piece.length)
      return {node, offset - left};
    else {
      offset -= left + nodes_[node].piece.length;
      node = nodes_[node].right;
    }
  }
  return {0, 0};
}

PieceTree::Offset PieceTree::LineBreaksBefore(Offset offset) const {
  offset = std::min(offset, Length());
  Offset result = 0;
  Index node = root_;
  while (node) {
    const Node& n = nodes_[node];
    const Offset left = nodes_[n.left].units;
    if (offset < left)
      node = n.left;
    else {
      result += nodes_[n.left].breaks;
      offset -= left;
      if (offset <= n.piece.length) {
        Piece prefix = n.piece;
        prefix.length = offset;
        return result + CountBreaks(prefix);
      }
      result += n.piece_breaks;
      offset -= n.piece.length;
      node = n.right;
    }
  }
  return result;
}

PieceTree::Offset PieceTree::LineStart(Offset line) const {
  if (!line) return 0;
  Offset offset = 0;
  Index node = root_;
  while (node) {
    const Node& n = nodes_[node];
    const Node& left = nodes_[n.left];
    if (line <= left.breaks) {
      node = n.left;
      continue;
    }
    line -= left.breaks;
    offset += left.units;
    if (line <= n.piece_breaks) {
      // The piece holds the break; its buffer's sorted break offsets find it.
      const auto& lines = line_starts_[n.piece.buffer];
      const auto first = std::lower_bound(lines.begin(), lines.end(), n.piece.start);
      return offset + (first[line - 1] - n.piece.start) + 1;
    }
    line -= n.piece_breaks;
    offset += n.piece.length;
    node = n.right;
  }
  return Length();
}

uint32_t PieceTree::StyleAt(Offset offset) const {
  if (!Length()) return 0;
  return nodes_[Locate(std::min(offset, Length() - 1)).node].piece.style;
}

bkfont::Vector<PieceTree::Piece> PieceTree::Slice(Offset start, Offset length) const {
  assert(start <= Length() && length <= Length() - start);
  bkfont::Vector<Piece> result;
  Location location = Locate(start);
  while (length && location.node) {
    Piece piece = nodes_[location.node].piece;
    piece.start += location.offset;
    piece.length = std::min(length, piece.length - location.offset);
    result.push_back(piece);
    length -= piece.length;
    location = {Successor(location.node), 0};
  }
  return result;
}

std::u16string PieceTree::Text() const {
  std::u16string text;
  text.reserve(Length());
  for (Index node = Minimum(root_); node; node = Successor(node)) text.append(View(nodes_[node].piece));
  return text;
}

PieceTree::Index PieceTree::Allocate(Piece piece) {
  Index index;
  if (free_head_) {
    index = free_head_;
    free_head_ = nodes_[index].next_free;
    nodes_[index] = Node{};
  } else {
    index = static_cast<Index>(nodes_.size());
    nodes_.emplace_back();
  }
  // Never retain a Node reference across Allocate(): vector growth moves the arena.
  nodes_[index].piece = piece;
  nodes_[index].units = piece.length;
  nodes_[index].breaks = nodes_[index].piece_breaks = CountBreaks(piece);
  nodes_[index].red = nodes_[index].live = true;
  ++live_nodes_;
  return index;
}

void PieceTree::Release(Index node) {
  assert(node && nodes_[node].live);
  nodes_[node] = Node{};
  nodes_[node].next_free = free_head_;
  free_head_ = node;
  --live_nodes_;
}

void PieceTree::Pull(Index node) {
  if (!node) return;
  Node& n = nodes_[node];
  n.units = nodes_[n.left].units + n.piece.length + nodes_[n.right].units;
  n.breaks = nodes_[n.left].breaks + n.piece_breaks + nodes_[n.right].breaks;
}
void PieceTree::PullAncestors(Index node) {
  while (node) {
    Pull(node);
    node = nodes_[node].parent;
  }
}
PieceTree::Index PieceTree::Minimum(Index node) const {
  while (nodes_[node].left) node = nodes_[node].left;
  return node;
}
PieceTree::Index PieceTree::Successor(Index node) const {
  if (nodes_[node].right) return Minimum(nodes_[node].right);
  Index parent = nodes_[node].parent;
  while (parent && node == nodes_[parent].right) {
    node = parent;
    parent = nodes_[parent].parent;
  }
  return parent;
}
PieceTree::Index PieceTree::Predecessor(Index node) const {
  if (!node) {
    node = root_;
    while (nodes_[node].right) node = nodes_[node].right;
    return node;
  }
  if (nodes_[node].left) {
    node = nodes_[node].left;
    while (nodes_[node].right) node = nodes_[node].right;
    return node;
  }
  Index parent = nodes_[node].parent;
  while (parent && node == nodes_[parent].left) {
    node = parent;
    parent = nodes_[parent].parent;
  }
  return parent;
}

void PieceTree::RotateLeft(Index x) {
  const Index y = nodes_[x].right;
  nodes_[x].right = nodes_[y].left;
  if (nodes_[y].left) nodes_[nodes_[y].left].parent = x;
  nodes_[y].parent = nodes_[x].parent;
  if (!nodes_[x].parent)
    root_ = y;
  else if (x == nodes_[nodes_[x].parent].left)
    nodes_[nodes_[x].parent].left = y;
  else
    nodes_[nodes_[x].parent].right = y;
  nodes_[y].left = x;
  nodes_[x].parent = y;
  Pull(x);
  Pull(y);
}
void PieceTree::RotateRight(Index x) {
  const Index y = nodes_[x].left;
  nodes_[x].left = nodes_[y].right;
  if (nodes_[y].right) nodes_[nodes_[y].right].parent = x;
  nodes_[y].parent = nodes_[x].parent;
  if (!nodes_[x].parent)
    root_ = y;
  else if (x == nodes_[nodes_[x].parent].right)
    nodes_[nodes_[x].parent].right = y;
  else
    nodes_[nodes_[x].parent].left = y;
  nodes_[y].right = x;
  nodes_[x].parent = y;
  Pull(x);
  Pull(y);
}

void PieceTree::FixInsert(Index node) {
  while (nodes_[nodes_[node].parent].red) {
    const Index parent = nodes_[node].parent, grand = nodes_[parent].parent;
    const bool left = parent == nodes_[grand].left;
    const Index uncle = left ? nodes_[grand].right : nodes_[grand].left;
    if (nodes_[uncle].red) {
      nodes_[parent].red = nodes_[uncle].red = false;
      nodes_[grand].red = true;
      node = grand;
    } else {
      if (node == (left ? nodes_[parent].right : nodes_[parent].left)) {
        node = parent;
        if (left)
          RotateLeft(node);
        else
          RotateRight(node);
      }
      const Index p = nodes_[node].parent, g = nodes_[p].parent;
      nodes_[p].red = false;
      nodes_[g].red = true;
      if (left)
        RotateRight(g);
      else
        RotateLeft(g);
    }
  }
  nodes_[root_].red = false;
}

PieceTree::Index PieceTree::InsertBefore(Index next, Piece piece) {
  const Index node = Allocate(piece);
  Index parent = next;
  if (!root_)
    root_ = node;
  else if (next && !nodes_[next].left)
    nodes_[next].left = node;
  else {
    parent = Predecessor(next);
    nodes_[parent].right = node;
  }
  nodes_[node].parent = parent;
  PullAncestors(parent);
  FixInsert(node);
  return node;
}

void PieceTree::Transplant(Index from, Index to) {
  const Index parent = nodes_[from].parent;
  if (!parent)
    root_ = to;
  else if (from == nodes_[parent].left)
    nodes_[parent].left = to;
  else
    nodes_[parent].right = to;
  if (to) nodes_[to].parent = parent;
}

void PieceTree::FixErase(Index node, Index parent) {
  while (node != root_ && !nodes_[node].red) {
    const bool left = node == nodes_[parent].left;
    Index sibling = left ? nodes_[parent].right : nodes_[parent].left;
    if (nodes_[sibling].red) {
      nodes_[sibling].red = false;
      nodes_[parent].red = true;
      if (left)
        RotateLeft(parent);
      else
        RotateRight(parent);
      sibling = left ? nodes_[parent].right : nodes_[parent].left;
    }
    if (!nodes_[nodes_[sibling].left].red && !nodes_[nodes_[sibling].right].red) {
      if (sibling) nodes_[sibling].red = true;
      node = parent;
      parent = nodes_[node].parent;
    } else {
      Index far = left ? nodes_[sibling].right : nodes_[sibling].left;
      if (!nodes_[far].red) {
        const Index near = left ? nodes_[sibling].left : nodes_[sibling].right;
        if (near) nodes_[near].red = false;
        nodes_[sibling].red = true;
        if (left)
          RotateRight(sibling);
        else
          RotateLeft(sibling);
        sibling = left ? nodes_[parent].right : nodes_[parent].left;
        far = left ? nodes_[sibling].right : nodes_[sibling].left;
      }
      nodes_[sibling].red = nodes_[parent].red;
      nodes_[parent].red = false;
      if (far) nodes_[far].red = false;
      if (left)
        RotateLeft(parent);
      else
        RotateRight(parent);
      node = root_;
      parent = 0;
    }
  }
  if (node) nodes_[node].red = false;
}

void PieceTree::Erase(Index z) {
  Index y = z, x = 0, parent = 0;
  bool red = nodes_[y].red;
  if (!nodes_[z].left || !nodes_[z].right) {
    x = nodes_[z].left ? nodes_[z].left : nodes_[z].right;
    parent = nodes_[z].parent;
    Transplant(z, x);
    PullAncestors(parent);
  } else {
    y = Minimum(nodes_[z].right);
    red = nodes_[y].red;
    x = nodes_[y].right;
    parent = nodes_[y].parent;
    if (parent == z) {
      parent = y;
      if (x) nodes_[x].parent = y;
    } else {
      Transplant(y, x);
      PullAncestors(parent);
      nodes_[y].right = nodes_[z].right;
      nodes_[nodes_[y].right].parent = y;
    }
    Transplant(z, y);
    nodes_[y].left = nodes_[z].left;
    nodes_[nodes_[y].left].parent = y;
    nodes_[y].red = nodes_[z].red;
    PullAncestors(y);
  }
  if (!red) FixErase(x, parent);
  Release(z);
}

PieceTree::Index PieceTree::Boundary(Offset position) {
  Location location = Locate(position);
  if (!location.node || !location.offset) return location.node;
  Piece suffix = nodes_[location.node].piece;
  suffix.start += location.offset;
  suffix.length -= location.offset;
  nodes_[location.node].piece.length = location.offset;
  nodes_[location.node].piece_breaks = CountBreaks(nodes_[location.node].piece);
  PullAncestors(location.node);
  return InsertBefore(Successor(location.node), suffix);
}

void PieceTree::Coalesce(Offset position) {
  Index right = Locate(position).node;
  Index left = Predecessor(right);
  if (!right || !left) return;
  const Piece a = nodes_[left].piece, b = nodes_[right].piece;
  if (a.buffer == b.buffer && a.style == b.style && a.start + a.length == b.start) {
    nodes_[left].piece.length += b.length;
    nodes_[left].piece_breaks += nodes_[right].piece_breaks;
    PullAncestors(left);
    Erase(right);
  }
}

void PieceTree::Replace(Offset start, Offset length, std::span<const Piece> replacement) {
  assert(start <= Length() && length <= Length() - start);
  // Splitting the end first keeps the starting offset stable.
  const Index end = Boundary(start + length);
  Index node = Boundary(start);
  while (node != end) {
    const Index next = Successor(node);
    Erase(node);
    node = next;
  }
  Offset added = 0;
  for (const Piece& piece : replacement) {
    if (!piece.length) continue;
    InsertBefore(end, piece);
    Coalesce(start + added);
    added += piece.length;
  }
  Coalesce(start + added);
}

PieceTree::Statistics PieceTree::Stats() const {
  return {live_nodes_, nodes_.size() - 1 - live_nodes_, nodes_.capacity() - 1,
          buffers_[0].size() + buffers_[1].size()};
}

bool PieceTree::Validate() const {
  if (nodes_.empty() || root_ >= nodes_.size() || nodes_[0].red || nodes_[0].live ||
      nodes_[0].units || nodes_[0].breaks || nodes_[root_].parent || nodes_[root_].red) return false;
  bkfont::Vector<bool> visited(nodes_.size());
  size_t live = 0, free = 0;
  const std::function<int(Index, Index)> visit = [&](Index index, Index parent) -> int {
    if (!index) return 1;
    if (index >= nodes_.size() || visited[index]) return -1;
    visited[index] = true;
    ++live;
    const Node& n = nodes_[index];
    if (!n.live || n.parent != parent || n.piece.buffer > 1 || !n.piece.length ||
        n.piece.start > buffers_[n.piece.buffer].size() ||
        n.piece.length > buffers_[n.piece.buffer].size() - n.piece.start) return -1;
    const int left = visit(n.left, index), right = visit(n.right, index);
    if (left < 0 || left != right) return -1;
    if (n.red && (nodes_[n.left].red || nodes_[n.right].red)) return -1;
    if (n.units != nodes_[n.left].units + n.piece.length + nodes_[n.right].units ||
        n.piece_breaks != CountBreaks(n.piece) ||
        n.breaks != nodes_[n.left].breaks + n.piece_breaks + nodes_[n.right].breaks) return -1;
    return left + !n.red;
  };
  if (visit(root_, 0) < 0) return false;
  for (Index index = free_head_; index; index = nodes_[index].next_free) {
    if (index >= nodes_.size() || visited[index] || nodes_[index].live) return false;
    visited[index] = true;
    ++free;
  }
  return live == live_nodes_ && live + free + 1 == nodes_.size();
}

} // namespace rich_text
