// Ported from: blink/renderer/platform/fonts/shaping/harfbuzz_face_from_typeface.cc

// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "harfbuzz_face_from_typeface.h"

#include "base/numerics/safe_conversions.h"
#include "platform/stream.h"

namespace {

static void DeleteTypefaceStream(void* stream_asset_ptr) {
  bkfont::StreamAsset* stream_asset =
      reinterpret_cast<bkfont::StreamAsset*>(stream_asset_ptr);
  delete stream_asset;
}

} // namespace

namespace bkfont {

hb::unique_ptr<hb_face_t> HbFaceFromTypeface(std::shared_ptr<Typeface> typeface) {
  hb::unique_ptr<hb_face_t> return_face(nullptr);
  int ttc_index = 0;

  // Have OpenStream() write the ttc index of this typeface within the stream
  // to the ttc_index parameter, so that we can check it below against the
  // count of faces within the buffer, as HarfBuzz counts it.
  std::unique_ptr<StreamAsset> tf_stream(typeface->OpenStream(&ttc_index));
  if (tf_stream && tf_stream->GetMemoryBase()) {
    const void* tf_memory = tf_stream->GetMemoryBase();
    std::size_t tf_size = tf_stream->GetLength();
    hb::unique_ptr<hb_blob_t> face_blob(hb_blob_create(
        reinterpret_cast<const char*>(tf_memory),
        base::checked_cast<unsigned int>(tf_size), HB_MEMORY_MODE_READONLY,
        tf_stream.release(), DeleteTypefaceStream));
    // hb_face_create always succeeds.
    // Use hb_face_count to retrieve the number of recognized faces in the
    // blob. hb_face_create_for_tables may still create a working hb_face.
    // See https://github.com/harfbuzz/harfbuzz/issues/248 .
    unsigned int num_hb_faces = hb_face_count(face_blob.get());
    if (0 < num_hb_faces && static_cast<unsigned>(ttc_index) < num_hb_faces) {
      return_face =
          hb::unique_ptr<hb_face_t>(hb_face_create(face_blob.get(), ttc_index));
    }
  }
  return return_face;
}

} // namespace bkfont
