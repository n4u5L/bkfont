// Ported from: skia/src/sfnt/SkOTUtils.h

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <utility>

#include "base/vector.h"
#include "typeface.h"

namespace bkfont {

// SkOTTableName::Iterator over the family name types (FontFamilyName,
// PreferredFamily, WWSFamilyName), in that order. Defined in
// opentype_name.cc.
Vector<Typeface::LocalizedString> FamilyNamesFromNameTable(std::span<const std::uint8_t> table);

// SkOTUtils::LocalizedStrings_NameTable. The records are decoded when the
// iterator is made; Next returns them in the upstream order.
class LocalizedStringsNameTable final : public Typeface::LocalizedStrings {
public:
  // Returns null if the typeface has no 'name' table or it cannot be read.
  static std::unique_ptr<LocalizedStringsNameTable> MakeForFamilyNames(const Typeface& typeface);

  bool Next(Typeface::LocalizedString* localized_string) override;

private:
  explicit LocalizedStringsNameTable(Vector<Typeface::LocalizedString> names);

  Vector<Typeface::LocalizedString> names_;
  wtf_size_t index_ = 0;
};

// SkOTUtils::LocalizedStrings_SingleName.
class LocalizedStringsSingleName final : public Typeface::LocalizedStrings {
public:
  LocalizedStringsSingleName(String name, String language)
      : name_(std::move(name)),
        language_(std::move(language)),
        has_next_(true) {
  }

  bool Next(Typeface::LocalizedString* localized_string) override {
    localized_string->string = name_;
    localized_string->language = language_;

    bool had_next = has_next_;
    has_next_ = false;
    return had_next;
  }

private:
  String name_;
  String language_;
  bool has_next_;
};

} // namespace bkfont
