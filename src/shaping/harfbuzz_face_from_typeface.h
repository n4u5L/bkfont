// Ported from: blink/renderer/platform/fonts/shaping/harfbuzz_face_from_typeface.h

// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include <hb.h>
#include <hb-cplusplus.hh>

#include "platform/typeface.h"

namespace bkfont {

// Creates a scoped HarfBuzz hb_face_t based on accessing the underlying Data
// of the Typeface (using Typeface::OpenStream() and
// Stream::GetMemoryBase()). Returns null when the stream has no memory base or
// HarfBuzz does not recognize the requested face in it.
hb::unique_ptr<hb_face_t> HbFaceFromTypeface(std::shared_ptr<Typeface> typeface);

} // namespace bkfont
