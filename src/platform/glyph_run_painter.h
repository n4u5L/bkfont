// Ported from: skia/src/core/SkGlyphRunPainter.h

#pragma once

#include <memory>
#include <span>

#include "mask.h"
#include "matrix.h"
#include "pixmap.h"
#include "rect.h"
#include "scaler_context.h"
#include "shader.h"
#include "surface_props.h"

namespace bkfont {

class Canvas;
class GlyphRunList;
class PlatformGlyph;
class PlatformPaint;

// SkGlyphRunListPainterCPU. Draws glyph runs on a raster device: large glyphs
// as paths or drawables through the canvas, the rest as device-space masks,
// and color glyphs that need a transform as scaled images.
class GlyphRunListPainterCPU {
public:
  // SkGlyphRunListPainterCPU::BitmapDevicePainter. The SkZip of glyphs and
  // positions is passed as two spans of the same size. The device draws into
  // its pixels, so the methods are not const.
  class BitmapDevicePainter {
  public:
    BitmapDevicePainter() = default;
    BitmapDevicePainter(const BitmapDevicePainter&) = default;
    virtual ~BitmapDevicePainter() = default;

    // Positions are integral device positions of the glyph origins.
    virtual void PaintMasks(std::span<const PlatformGlyph* const> glyphs,
                            std::span<const ScalarPoint> positions,
                            const PlatformPaint& paint) = 0;
    virtual void DrawBitmap(std::shared_ptr<const Image> image, const ScalarMatrix& matrix, const ScalarRect* dst_or_null,
                            const SamplingOptions& sampling, const PlatformPaint& paint) = 0;
  };

  // The color space of the device is always the legacy sRGB-encoded space.
  GlyphRunListPainterCPU(const SurfaceProps& props, ColorType color_type);

  void DrawForBitmapDevice(Canvas* canvas, BitmapDevicePainter* bitmap_device,
                           const GlyphRunList& glyph_run_list, const PlatformPaint& paint,
                           const ScalarMatrix& draw_matrix);

private:
  // The props as on the actual device.
  const SurfaceProps device_props_;

  // The props for when the bitmap device can't draw LCD text.
  const SurfaceProps bitmap_fallback_props_;
  const ColorType color_type_;
  const ScalerContextFlags scaler_context_flags_;
};

// SkBitmap::installPixels(SkImageInfo::MakeN32Premul(...)) over a kARGB32
// glyph mask. Images own their pixels, so the mask is copied.
std::shared_ptr<const Image> MakeImageFromARGB32Mask(const Mask& mask);

} // namespace bkfont
