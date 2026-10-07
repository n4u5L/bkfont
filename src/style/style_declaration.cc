// Adapted from core/css/css_property_value_set.cc (MutableCSSPropertyValueSet)
// and CSSPropertyParser::ParseCSSWideKeyword().
#include "style_declaration.h"

#include <algorithm>

#include "style/css_inherited_value.h"
#include "style/css_initial_value.h"
#include "style/css_unset_value.h"

namespace bkfont {
namespace {

// ConsumeCSSWideKeyword().
scoped_refptr<const CSSValue> CreateWideKeywordValue(CSSWideKeyword keyword) {
  switch (keyword) {
    case CSSWideKeyword::kInitial: return CSSInitialValue::Create();
    case CSSWideKeyword::kInherit: return CSSInheritedValue::Create();
    case CSSWideKeyword::kUnset: return cssvalue::CSSUnsetValue::Create();
  }
  return nullptr;
}

} // namespace

std::span<const StyleDeclaration::Entry> StyleDeclaration::Entries() const {
  return entries_ ? std::span<const Entry>(entries_->data(), entries_->size()) : std::span<const Entry>();
}

StyleDeclaration::EntryVector& StyleDeclaration::Access() {
  if (!entries_) entries_ = std::make_shared<EntryVector>();
  else if (entries_.use_count() != 1) entries_ = std::make_shared<EntryVector>(*entries_);
  return *entries_;
}

int StyleDeclaration::FindPropertyIndex(CSSPropertyID id) const {
  const auto entries = Entries();
  for (size_t i = 0; i < entries.size(); ++i)
    if (entries[i].property == id) return static_cast<int>(i);
  return -1;
}

int StyleDeclaration::FindInsertionPointForID(CSSPropertyID id) {
  const int to_replace = FindPropertyIndex(id);
  if (to_replace < 0) return -1;
  if (!GetCSSPropertyMetadata(id)->logical_property_group.empty()) {
    const auto entries = Entries();
    for (int n = static_cast<int>(entries.size()) - 1; n > to_replace; --n) {
      if (IsInSameLogicalPropertyGroupWithDifferentMappingLogic(id, entries[n].property)) {
        Access().EraseAt(static_cast<wtf_size_t>(to_replace));
        return -1;
      }
    }
  }
  return to_replace;
}

const CSSValue* StyleDeclaration::Get(CSSPropertyID id) const {
  const int index = FindPropertyIndex(ResolveCSSPropertyID(id));
  return index < 0 ? nullptr : Entries()[index].value.get();
}

void StyleDeclaration::StoreLonghand(CSSPropertyID id, scoped_refptr<const CSSValue> value) {
  Entry entry{id, std::move(value)};
  const int to_replace = FindInsertionPointForID(id);
  if (to_replace < 0) {
    Access().push_back(std::move(entry));
  } else if (!(Entries()[to_replace] == entry)) {
    Access()[static_cast<wtf_size_t>(to_replace)] = std::move(entry);
  }
}

bool StyleDeclaration::SetCSSWideKeyword(CSSPropertyID id, CSSWideKeyword keyword) {
  id = ResolveCSSPropertyID(id);
  const CSSPropertyMetadata* metadata = GetCSSPropertyMetadata(id);
  if (!metadata) return false;
  scoped_refptr<const CSSValue> value = CreateWideKeywordValue(keyword);
  // AddExpandedPropertyForValue(): every longhand of a shorthand.
  if (IsShorthand(id)) {
    for (CSSPropertyID longhand : metadata->longhands) StoreLonghand(longhand, value);
  } else {
    StoreLonghand(id, std::move(value));
  }
  return true;
}

bool StyleDeclaration::Remove(CSSPropertyID id) {
  id = ResolveCSSPropertyID(id);
  if (IsShorthand(id)) {
    bool removed = false;
    for (CSSPropertyID longhand : GetCSSPropertyMetadata(id)->longhands) removed |= Remove(longhand);
    return removed;
  }
  const int index = FindPropertyIndex(id);
  if (index < 0) return false;
  Access().EraseAt(static_cast<wtf_size_t>(index));
  return true;
}

void StyleDeclaration::Merge(const StyleDeclaration& other) {
  // Into an empty block, the merge gives the same entries in the same order,
  // so the vector is shared.
  if (IsEmpty()) {
    entries_ = other.entries_;
    return;
  }
  // `other` may be this block.
  const StyleDeclaration snapshot(other);
  for (const Entry& entry : snapshot.Entries()) StoreLonghand(entry.property, entry.value);
}

bool StyleDeclaration::operator==(const StyleDeclaration& other) const {
  return entries_ == other.entries_ || std::ranges::equal(Entries(), other.Entries());
}

} // namespace bkfont
