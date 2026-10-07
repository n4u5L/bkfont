// Ported from: skia/src/sfnt/SkOTUtils.cpp

#include "ot_utils.h"

#include <utility>

namespace bkit {

namespace {

constexpr std::uint32_t kNameTag = ('n' << 24) | ('a' << 16) | ('m' << 8) | 'e';

} // namespace

LocalizedStringsNameTable::LocalizedStringsNameTable(Vector<Typeface::LocalizedString> names)
    : names_(std::move(names)) {
}

std::unique_ptr<LocalizedStringsNameTable> LocalizedStringsNameTable::MakeForFamilyNames(const Typeface& typeface) {
  std::size_t name_table_size = typeface.GetTableSize(kNameTag);
  if (0 == name_table_size) {
    return nullptr;
  }
  std::unique_ptr<std::uint8_t[]> name_table_data(new std::uint8_t[name_table_size]);
  std::size_t copied = typeface.GetTableData(kNameTag, 0, name_table_size, name_table_data.get());
  if (copied != name_table_size) {
    return nullptr;
  }

  return std::unique_ptr<LocalizedStringsNameTable>(new LocalizedStringsNameTable(
      FamilyNamesFromNameTable(std::span<const std::uint8_t>(name_table_data.get(), name_table_size))));
}

bool LocalizedStringsNameTable::Next(Typeface::LocalizedString* localized_string) {
  if (index_ >= names_.size()) {
    return false;
  }
  *localized_string = names_[index_++];
  return true;
}

} // namespace bkit
