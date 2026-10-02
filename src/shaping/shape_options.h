// Ported from Chromium: third_party/blink/renderer/platform/fonts/shaping/shape_options.h
// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace blink {

//
// Options when shaping by `HarfBuzzShaper`.
//
struct ShapeOptions {
  bool is_line_start = false;
  bool han_kerning_start = false;
  bool han_kerning_end = false;
};

} // namespace blink
