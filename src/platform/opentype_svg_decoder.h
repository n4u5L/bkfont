// Ported from: skia/include/core/SkOpenTypeSVGDecoder.h
// Ported from: skia/include/core/SkGraphics.h
// Ported from: skia/src/core/SkGraphics.cpp

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "paint/mask_gamma.h"

namespace bkit {

class Canvas;

// SkOpenTypeSVGDecoder.
class OpenTypeSVGDecoder {
public:
  // Each instance probably owns an SVG DOM. The instance may be cached so
  // needs to report how much memory it retains.
  virtual std::size_t ApproximateSize() = 0;
  virtual bool Render(Canvas& canvas, int upem, std::uint16_t glyph_id,
                      ColorARGB foreground_color, std::span<ColorARGB> palette) = 0;
  virtual ~OpenTypeSVGDecoder() = default;
};

// SkGraphics::OpenTypeSVGDecoderFactory. Without a factory, scalable faces do
// not load OpenType SVG glyphs and their other representations are used.
using OpenTypeSVGDecoderFactory = std::unique_ptr<OpenTypeSVGDecoder> (*)(const std::uint8_t* svg, std::size_t length);

// SkGraphics::SetOpenTypeSVGDecoderFactory: installs the factory and returns
// the previous one. Install it before any scaler context is created.
OpenTypeSVGDecoderFactory SetOpenTypeSVGDecoderFactory(OpenTypeSVGDecoderFactory factory);
OpenTypeSVGDecoderFactory GetOpenTypeSVGDecoderFactory();

} // namespace bkit
