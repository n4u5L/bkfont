// Subset of blink/renderer/core/css/resolver/cascade_map.h. Keeps only the
// winning priority per longhand: without 'revert' or cascade layers, lower
// priorities are never looked up again. No custom properties.
#pragma once

#include <array>
#include <bitset>
#include <cassert>

#include "style/cascade_priority.h"
#include "style/style_declaration.h"

namespace bkit {

class CascadeMap {
public:
  static constexpr size_t kNumCSSProperties = static_cast<size_t>(CSSPropertyID::kCount);

  bool Has(CSSPropertyID id) const { return native_properties_.test(static_cast<size_t>(id)); }
  CascadePriority* Find(CSSPropertyID id) {
    return Has(id) ? &priorities_[static_cast<size_t>(id)] : nullptr;
  }
  // Keeps the higher of the existing and the incoming priority.
  void Add(CSSPropertyID id, CascadePriority priority) {
    assert(IsLonghand(id));
    const auto index = static_cast<size_t>(id);
    if (!native_properties_.test(index)) {
      native_properties_.set(index);
      priorities_[index] = priority;
      return;
    }
    if (priorities_[index] >= priority) return;
    priorities_[index] = priority;
  }
  void Reset() { native_properties_.reset(); }

private:
  std::bitset<kNumCSSProperties> native_properties_;
  std::array<CascadePriority, kNumCSSProperties> priorities_;
};

} // namespace bkit
