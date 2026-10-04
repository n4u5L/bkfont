// Copyright 2016 The Chromium Authors
// BSD-style license; derived from shaping/harfbuzz_face_from_typeface.cc.
#include "font_table_harfbuzz.h"
namespace bkfont {
namespace {
hb_blob_t* ReferenceTable(hb_face_t*, hb_tag_t tag, void* data) {
  const auto& face = *static_cast<std::shared_ptr<FontFace>*>(data);
  auto bytes = std::make_unique<Vector<uint8_t>>(face->TableData(tag));
  if (bytes->empty()) return hb_blob_get_empty();
  const char* buffer = reinterpret_cast<const char*>(bytes->data());
  const unsigned size = static_cast<unsigned>(bytes->size());
  return hb_blob_create(buffer, size, HB_MEMORY_MODE_READONLY, bytes.release(), [](void* value) { delete static_cast<Vector<uint8_t>*>(value); });
}
} // namespace
hb_face_t* HbFaceFromFontFace(std::shared_ptr<FontFace> face) {
  if (!face) return nullptr;
  auto* owner = new std::shared_ptr<FontFace>(std::move(face));
  hb_face_t* result = hb_face_create_for_tables(ReferenceTable, owner, [](void* value) { delete static_cast<std::shared_ptr<FontFace>*>(value); });
  hb_face_set_index(result, (*owner)->CollectionIndex());
  return result;
}
} // namespace bkfont
