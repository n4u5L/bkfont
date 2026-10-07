// Ported from: blink/renderer/platform/fonts/opentype/font_format_check.cc
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "font_format_check.h"

// Include HarfBuzz to have a cross-platform way to retrieve table tags without
// having to rely on the platform being able to instantiate this font format.
#include <hb.h>

#include <hb-cplusplus.hh>

#include <algorithm>
#include <memory>
#include <span>

#include "platform/typeface.h"
namespace bkit {

namespace {

bool ContainsTableTag(std::span<const uint32_t> tags, uint32_t tag) {
  return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

FontFormatCheck::COLRVersion determineCOLRVersion(
    std::span<const uint32_t> table_tags,
    const hb_face_t* face) {
  const hb_tag_t kCOLRTag = HB_TAG('C', 'O', 'L', 'R');

  // Only try to read version if header size is sufficient.
  // https://docs.microsoft.com/en-us/typography/opentype/spec/colr#header
  const unsigned int kMinCOLRHeaderSize = 14;
  if (table_tags.size() && ContainsTableTag(table_tags, kCOLRTag) && ContainsTableTag(table_tags, HB_TAG('C', 'P', 'A', 'L'))) {
    hb::unique_ptr<hb_blob_t> table_blob(
        hb_face_reference_table(face, kCOLRTag));
    if (hb_blob_get_length(table_blob.get()) < kMinCOLRHeaderSize)
      return FontFormatCheck::COLRVersion::kNoCOLR;

    unsigned required_bytes_count = 2u;
    const char* colr_ptr =
        hb_blob_get_data(table_blob.get(), &required_bytes_count);
    std::span<const uint8_t> colr_data = std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(colr_ptr), required_bytes_count);

    if (colr_data.size() < 2u) {
      return FontFormatCheck::COLRVersion::kNoCOLR;
    }
    uint16_t colr_version = static_cast<uint16_t>((colr_data[0] << 8) | colr_data[1]);

    if (colr_version == 0)
      return FontFormatCheck::COLRVersion::kCOLRV0;
    else if (colr_version == 1)
      return FontFormatCheck::COLRVersion::kCOLRV1;
  }
  return FontFormatCheck::COLRVersion::kNoCOLR;
}

} // namespace

FontFormatCheck::FontFormatCheck(std::span<const uint8_t> sk_data) {
  hb::unique_ptr<hb_blob_t> font_blob(
      hb_blob_create(reinterpret_cast<const char*>(sk_data.data()),
                     static_cast<unsigned>(sk_data.size()),
                     HB_MEMORY_MODE_READONLY,
                     nullptr,
                     nullptr));
  hb::unique_ptr<hb_face_t> face(hb_face_create(font_blob.get(), 0));

  unsigned table_count = 0;
  table_count = hb_face_get_table_tags(face.get(), 0, nullptr, nullptr);
  table_tags_count_ = table_count;
  table_tags_ = std::make_unique<uint32_t[]>(table_tags_count_);
  if (!hb_face_get_table_tags(face.get(), 0, &table_count, table_tags_.get())) {
    table_tags_.reset();
    table_tags_count_ = 0;
  }

  colr_version_ = determineCOLRVersion(TableTags(), face.get());
}

FontFormatCheck::FontFormatCheck(const FontFormatCheck& other)
    : table_tags_(std::make_unique<uint32_t[]>(other.table_tags_count_)),
      table_tags_count_(other.table_tags_count_),
      colr_version_(other.colr_version_) {
  std::copy_n(other.table_tags_.get(), table_tags_count_, table_tags_.get());
}

FontFormatCheck& FontFormatCheck::operator=(const FontFormatCheck& other) {
  if (this == &other) return *this;
  auto table_tags = std::make_unique<uint32_t[]>(other.table_tags_count_);
  std::copy_n(other.table_tags_.get(), other.table_tags_count_, table_tags.get());
  table_tags_ = std::move(table_tags);
  table_tags_count_ = other.table_tags_count_;
  colr_version_ = other.colr_version_;
  return *this;
}

bool FontFormatCheck::IsVariableFont() const {
  return table_tags_count_ && ContainsTableTag(TableTags(), HB_TAG('f', 'v', 'a', 'r'));
}

bool FontFormatCheck::IsCbdtCblcColorFont() const {
  return table_tags_count_ && ContainsTableTag(TableTags(), HB_TAG('C', 'B', 'D', 'T')) && ContainsTableTag(TableTags(), HB_TAG('C', 'B', 'L', 'C'));
}

bool FontFormatCheck::IsColrCpalColorFontV0() const {
  return colr_version_ == COLRVersion::kCOLRV0;
}

bool FontFormatCheck::IsColrCpalColorFontV1() const {
  return colr_version_ == COLRVersion::kCOLRV1;
}

bool FontFormatCheck::IsVariableColrV0Font() const {
  return IsColrCpalColorFontV0() && IsVariableFont();
}

bool FontFormatCheck::IsSbixColorFont() const {
  return table_tags_count_ && ContainsTableTag(TableTags(), HB_TAG('s', 'b', 'i', 'x'));
}

bool FontFormatCheck::IsCff2OutlineFont() const {
  return table_tags_count_ && ContainsTableTag(TableTags(), HB_TAG('C', 'F', 'F', '2'));
}

bool FontFormatCheck::IsColorFont() const {
  return IsCbdtCblcColorFont() || IsColrCpalColorFont() || IsSbixColorFont();
}

FontFormatCheck::VariableFontSubType FontFormatCheck::ProbeVariableFont(
    std::shared_ptr<Typeface> typeface) {
  if (!typeface->GetTableSize(HB_TAG('f', 'v', 'a', 'r')))
    return VariableFontSubType::kNotVariable;

  if (typeface->GetTableSize(HB_TAG('C', 'F', 'F', '2')))
    return VariableFontSubType::kVariableCFF2;
  return VariableFontSubType::kVariableTrueType;
}

} // namespace bkit
