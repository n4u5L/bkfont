#pragma once

#include <algorithm>
#include <cstdint>

#include "base/vector.h"

namespace bkit::example {

// Rows of a page (device pixels from its top) whose pixels may differ from
// what was painted. A row range meeting the previous one merges with it, so
// damage added in page order stays short.
class RowDamage {
public:
  struct Rows {
    float top, bottom;
  };

  void Add(float top, float bottom) {
    if (!rows_.empty() && top <= rows_.back().bottom && bottom >= rows_.back().top) {
      rows_.back() = {std::min(top, rows_.back().top), std::max(bottom, rows_.back().bottom)};
      return;
    }
    rows_.push_back(Rows{top, bottom});
  }
  bool Empty() const {
    return rows_.empty();
  }
  // Returns the damage and clears it.
  Vector<Rows> Take() {
    Vector<Rows> rows;
    rows.swap(rows_);
    return rows;
  }

private:
  Vector<Rows> rows_;
};

// Calls `damage(start, end)` for the text offsets whose highlight differs
// between the painted selection [painted_start, painted_end) and the next
// one; an empty selection paints no highlight.
template <typename Damage>
void ForChangedHighlight(uint32_t painted_start, uint32_t painted_end, uint32_t next_start, uint32_t next_end,
                         const Damage& damage) {
  const bool painted_empty = painted_start == painted_end, next_empty = next_start == next_end;
  if (painted_empty != next_empty) {
    if (painted_empty)
      damage(next_start, next_end);
    else
      damage(painted_start, painted_end);
  } else if (!next_empty) {
    // Only offsets between the old and the new ends change highlight.
    if (painted_start != next_start) damage(std::min(painted_start, next_start), std::max(painted_start, next_start));
    if (painted_end != next_end) damage(std::min(painted_end, next_end), std::max(painted_end, next_end));
  }
}

} // namespace bkit::example
