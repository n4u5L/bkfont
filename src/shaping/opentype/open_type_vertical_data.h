// Ported from Chromium: third_party/blink/renderer/platform/fonts/opentype/open_type_vertical_data.h
/*
 * Copyright (C) 2012 Koji Ishii <kojiishi@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include "font/glyph.h"

#include "wtf/hash_map.h"
#include "wtf/vector.h"

namespace blink {
class FontPlatformData;
class FontFace;

class OpenTypeVerticalData
    : public std::enable_shared_from_this<OpenTypeVerticalData> {
public:
  explicit OpenTypeVerticalData(std::shared_ptr<FontFace>);

  void SetScaleAndFallbackMetrics(float size_per_unit,
                                  float ascent,
                                  int height);

  bool IsOpenType() const {
    return !advance_widths_.empty();
  }
  bool HasVerticalMetrics() const {
    return !advance_heights_.empty();
  }
  float AdvanceHeight(Glyph) const;

  void GetVerticalTranslationsForGlyphs(const FontPlatformData&,
                                        const Glyph*,
                                        size_t,
                                        float* out_xy_array) const;

private:
  void LoadMetrics(std::shared_ptr<FontFace>);
  bool HasVORG() const {
    return !vert_origin_y_.empty();
  }

  HashMap<Glyph, Glyph> vertical_glyph_map_;
  Vector<uint16_t> advance_widths_;
  Vector<uint16_t> advance_heights_;
  Vector<int16_t> top_side_bearings_;
  int16_t default_vert_origin_y_;
  HashMap<Glyph, int16_t> vert_origin_y_;

  float size_per_unit_;
  float ascent_fallback_;
  int height_fallback_;
};

} // namespace blink
