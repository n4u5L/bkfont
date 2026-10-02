// Copyright 2016 The Chromium Authors
// BSD-style license; derived from shaping/harfbuzz_face_from_typeface.cc.
#pragma once
#include "platform/font_face.h"
#include <hb.h>
namespace blink {
// The returned face owns a shared reference to the source FontFace.
hb_face_t* HbFaceFromFontFace(std::shared_ptr<FontFace>);
} // namespace blink
