// Ported from: blink/renderer/platform/fonts/character_range.h
// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace bkit {

struct CharacterRange {
  CharacterRange(float from, float to, float ascent, float descent)
      : start(from),
        end(to),
        ascent(ascent),
        descent(descent) {
  }

  float Width() const {
    return end - start;
  }
  float Height() const {
    return ascent + descent;
  }

  float start;
  float end;

  float ascent;
  float descent;
};

} // namespace bkit
