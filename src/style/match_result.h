// Subset of blink/renderer/core/css/resolver/match_result.h and
// cascade_expansion.h. A MatchResult lists the declaration blocks that apply
// to one node, in cascade order; the cascade refers to a declaration by its
// position here and never copies its value.
#pragma once

#include <cassert>
#include <cstdint>

#include "base/vector.h"
#include "style/cascade_origin.h"
#include "style/style_declaration.h"

namespace bkit {

struct MatchedProperties {
  // Not owned; must outlive the cascade and remain unchanged while it is used.
  const StyleDeclaration* properties;
  CascadeOrigin origin;
};

class MatchResult {
public:
  // Blocks are added in origin order (user agent, then author) and, within
  // an origin, in increasing precedence. A null block (e.g. a named rule
  // which is not registered yet) adds nothing.
  void AddMatchedProperties(const StyleDeclaration* properties, CascadeOrigin origin) {
    assert(matched_properties_.empty() || origin >= matched_properties_.back().origin);
    if (properties) matched_properties_.push_back(MatchedProperties{properties, origin});
  }
  const Vector<MatchedProperties, 8>& GetMatchedProperties() const { return matched_properties_; }

private:
  Vector<MatchedProperties, 8> matched_properties_;
};

inline uint32_t EncodeMatchResultPosition(uint16_t block, uint16_t declaration) {
  return (static_cast<uint32_t>(block) << 16) | declaration;
}
inline wtf_size_t DecodeMatchedPropertiesIndex(uint32_t position) {
  return (position >> 16) & 0xFFFF;
}
inline wtf_size_t DecodeDeclarationIndex(uint32_t position) {
  return position & 0xFFFF;
}

} // namespace bkit
