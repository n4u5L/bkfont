// Local model adapter for Blink's OffsetMapping. Input text is preserved.
#include "offset_mapping.h"

#include <algorithm>
#include <unicode/uchar.h>

namespace bkfont {

const OffsetMapping::Unit* OffsetMapping::GetUnit(InlineNodeId id) const {
  const auto found = index_.find(id);
  return found == index_.end() ? nullptr : &units_[found->second];
}

std::optional<unsigned> OffsetMapping::GetTextContentOffset(const InlinePosition& position) const {
  if (const Unit* unit = GetUnit(position.node)) {
    if (position.offset <= unit->end - unit->start) return unit->start + position.offset;
    return std::nullopt;
  }
  const auto found = container_offsets_.find(position.node);
  if (found != container_offsets_.end() && position.offset < found->second.size())
    return found->second[position.offset];
  return std::nullopt;
}

InlinePosition OffsetMapping::GetPosition(unsigned offset, TextAffinity affinity) const {
  if (offset > text_.length()) return {};
  const Unit* candidate = nullptr;
  for (const Unit& unit : units_) {
    if (offset < unit.start) break;
    if (offset > unit.end) continue;
    candidate = &unit;
    if (offset < unit.end || affinity == TextAffinity::kUpstream) break;
  }
  if (!candidate) return {};
  return {candidate->object->Id(), offset - candidate->start, affinity};
}

bool OffsetMapping::HasBidiControlCharactersOnly(unsigned start, unsigned end) const {
  if (start > end || end > text_.length()) return false;
  for (unsigned i = start; i < end; ++i)
    if (!u_hasBinaryProperty(text_[i], UCHAR_BIDI_CONTROL)) return false;
  return true;
}

} // namespace bkfont
