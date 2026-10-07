// Ported from: skia/src/core/SkGraphics.cpp

#include "opentype_svg_decoder.h"

namespace bkit {

namespace {

OpenTypeSVGDecoderFactory g_svg_decoder_factory = nullptr;

} // namespace

OpenTypeSVGDecoderFactory SetOpenTypeSVGDecoderFactory(OpenTypeSVGDecoderFactory factory) {
  OpenTypeSVGDecoderFactory old(g_svg_decoder_factory);
  g_svg_decoder_factory = factory;
  return old;
}

OpenTypeSVGDecoderFactory GetOpenTypeSVGDecoderFactory() {
  return g_svg_decoder_factory;
}

} // namespace bkit
