// MutableCSSPropertyValueSet equivalent for typed, ordered declarations.
//
// This is the low-level style input: there is no CSS parser, so callers
// give each property its typed input, and the property's Make()
// (css_properties.h) builds the CSSValue its parser would. Shorthands are
// expanded when set, as CSSPropertyParser does; the block stores only
// longhands.
#pragma once

#include <memory>
#include <span>

#include "base/heap_vector.h"
#include "base/memory/scoped_refptr.h"
#include "style/css_properties.h"
#include "style/css_property_names.h"
#include "style/css_value.h"

namespace bkit {

// The CSS-wide keywords a declaration can take (ConsumeCSSWideKeyword()),
// without revert and revert-layer: there are no cascade origins to revert to.
enum class CSSWideKeyword {
  kInitial,
  kInherit,
  kUnset,
};

class StyleDeclaration {
public:
  using Entry = CSSPropertyValue;

  std::span<const Entry> Entries() const;
  // Stored longhand values. Shorthands expand when set and have no entry.
  const CSSValue* Get(CSSPropertyID) const;
  bool IsEmpty() const {
    return Entries().empty();
  }
  // Builds the value with the property's Make() and stores it. A shorthand
  // stores the longhands its Make() gives, in that order. Returns false
  // without changes for invalid input.
  //
  // Entries are kept in write order with at most one per longhand
  // (MutableCSSPropertyValueSet::SetLonghandProperty()): an existing entry is
  // replaced in place, unless a later entry is in the same logical property
  // group with the other mapping logic. Then the old entry is removed and the
  // new one appended, so the later of a logical/physical pair still wins in
  // the cascade.
  template <typename Property>
  [[nodiscard]] bool Set(const typename Property::Input& input) {
    if constexpr (IsShorthand(Property::kId)) {
      CSSPropertyValues properties;
      if (!Property::Make(input, properties)) return false;
      for (CSSPropertyValue& property : properties) StoreLonghand(property.property, std::move(property.value));
    } else {
      scoped_refptr<const CSSValue> value = Property::Make(input);
      if (!value) return false;
      StoreLonghand(Property::kId, std::move(value));
    }
    return true;
  }
  // CSSPropertyParser::ParseCSSWideKeyword(): the keyword on the longhand, or
  // on every longhand of a shorthand. Aliases resolve to their property.
  // Returns false for an unknown property.
  [[nodiscard]] bool SetCSSWideKeyword(CSSPropertyID, CSSWideKeyword);
  // MutableCSSPropertyValueSet::RemoveProperty(): a shorthand removes its
  // longhands. Aliases resolve to their property.
  bool Remove(CSSPropertyID);
  // MutableCSSPropertyValueSet::MergeAndOverrideOnConflict().
  void Merge(const StyleDeclaration&);
  bool operator==(const StyleDeclaration&) const;

private:
  // MutableCSSPropertyValueSet's property_vector_: at most one entry per
  // property, searched linearly. Copies share it until written.
  using EntryVector = HeapVector<Entry, 4>;
  // MutableCSSPropertyValueSet::SetLonghandProperty().
  void StoreLonghand(CSSPropertyID, scoped_refptr<const CSSValue>);
  int FindPropertyIndex(CSSPropertyID) const;
  // MutableCSSPropertyValueSet::FindInsertionPointForID(): the index to
  // replace, or -1 to append. May remove the existing entry.
  int FindInsertionPointForID(CSSPropertyID);
  EntryVector& Access();
  std::shared_ptr<EntryVector> entries_;
};

} // namespace bkit
