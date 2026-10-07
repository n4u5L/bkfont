// Local implementation: the CPU canvas that draws glyph images and text
// blobs, standing in for SkCanvas on an SkBitmapDevice. Pixels are kept as
// premultiplied floats while drawing and written back to the pixmap by Flush.
// Layers, clips, blend modes and coverage follow the raster pipeline; scan
// conversion is not bit-exact with Skia (see path_rasterizer.h). Text is
// drawn as SkBitmapDevice draws it: see glyph_run_painter.h.

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "canvas.h"
#include "glyph_run_painter.h"
#include "mask.h"
#include "path_rasterizer.h"
#include "surface_props.h"

namespace bkfont {

class RasterCanvas final : public Canvas, private GlyphRunListPainterCPU::BitmapDevicePainter {
public:
  // Reuses the base device allocation across sequential, bounded repaints.
  // Must outlive the canvas and must not be shared by live canvases.
  class ScratchBuffer {
  public:
    std::size_t CapacityBytes() const {
      return pixels_.capacity() * sizeof(PMColor4f);
    }

  private:
    friend class RasterCanvas;
    std::vector<PMColor4f> pixels_;
  };

  // pixmap must be kAlpha8 or kN32 and stay valid until Flush or
  // destruction. Its current pixels are the initial contents. The default
  // props have an unknown pixel geometry, so text is never drawn with LCD
  // masks, as SkCanvas(const SkBitmap&).
  explicit RasterCanvas(const Pixmap& pixmap);
  // An initial clear discards old pixels without widening pixels that will
  // immediately be overwritten. Omission preserves the pixmap as before.
  // A device origin lets a sub-pixmap retain global raster coordinates, like
  // a bounded layer. It avoids changing curve subdivision or glyph hinting
  // when repainting only part of a device.
  RasterCanvas(const Pixmap& pixmap, const SurfaceProps& props, std::optional<ColorARGB> initial_clear = std::nullopt,
               int origin_x = 0, int origin_y = 0, ScratchBuffer* scratch = nullptr);
  ~RasterCanvas() override;
  RasterCanvas(const RasterCanvas&) = delete;
  RasterCanvas& operator=(const RasterCanvas&) = delete;

  // Writes the pixels back to the pixmap, rounding to 8 bits.
  void Flush();
  // Writes only the intersection with the base device; coordinates include
  // its origin. Drawing state and float pixels remain available for reuse.
  void Flush(const IntRect& bounds);

  int Save() override;
  int SaveLayer(const ScalarRect* bounds, const PlatformPaint* paint) override;
  void Restore() override;
  int GetSaveCount() const override;

  void Concat(const ScalarMatrix& matrix) override;
  const ScalarMatrix& GetTotalMatrix() const override;

  void ClipRect(const ScalarRect& rect, bool do_anti_alias) override;
  void ClipPath(const ScalarPath& path, bool do_anti_alias) override;
  void ClipOutRect(const ScalarRect&, bool do_anti_alias) override;
  void ClipOutPath(const ScalarPath&, bool do_anti_alias) override;

  void DrawPaint(const PlatformPaint& paint) override;
  void DrawPath(const ScalarPath& path, const PlatformPaint& paint) override;
  void DrawImage(std::shared_ptr<const Image> image, float x, float y,
                 const SamplingOptions& sampling, const PlatformPaint* paint) override;

protected:
  // SkCanvas::onDrawGlyphRunList.
  void OnDrawGlyphRunList(const GlyphRunList& glyph_run_list, const PlatformPaint& paint) override;

private:
  struct Layer {
    IntRect bounds;
    std::vector<PMColor4f> pixels;
    // The paint the layer is composited with when restored.
    BlendMode blend_mode = BlendMode::kSrcOver;
    float alpha = 1;
    std::shared_ptr<const ImageFilter> image_filter;
    ScalarMatrix filter_matrix;
    // The color type and props of the SkBitmapDevice of the layer. Layers
    // are N32 and do not preserve LCD text.
    ColorType color_type = ColorType::kN32;
    SurfaceProps props;

    std::size_t Index(int x, int y) const {
      return static_cast<std::size_t>(y - bounds.top) * static_cast<std::size_t>(bounds.Width()) + static_cast<std::size_t>(x - bounds.left);
    }
    PMColor4f& At(int x, int y) {
      return pixels[Index(x, y)];
    }
    const PMColor4f& At(int x, int y) const {
      return pixels[Index(x, y)];
    }
  };

  struct Clip {
    // Conservative device bounds of the clip.
    IntRect bounds;
    // Only pixels differing from full coverage need storage. Outside this
    // mask (but inside clip bounds), coverage is one. Save shares the mask;
    // clip mutations detach it before writing.
    std::shared_ptr<CoverageMask> coverage;
  };

  struct State {
    ScalarMatrix matrix;
    Clip clip;
    // The layer started by the SaveLayer that pushed this state, if any.
    std::unique_ptr<Layer> layer;
  };

  float ClipCoverage(const Clip& clip, int x, int y) const;
  void IntersectClip(CoverageMask mask);
  Layer& TopLayer();

  // Draws src over the pixels of geometry (or the whole clip when null),
  // modulated by the clip. The shader is evaluated with ctm.
  void Fill(const CoverageMask* geometry, const PlatformPaint& paint, const ScalarMatrix& ctm);
  void CompositeLayer(const Layer& layer, const Clip& clip, Layer* target);

  // SkCanvas::internalQuickReject and SkCanvas::quickReject.
  bool InternalQuickReject(const ScalarRect& bounds, const PlatformPaint& paint) const;
  bool QuickReject(const ScalarRect& src) const;

  // SkDevice::drawGlyphRunList and SkDevice::simplifyGlyphRunRSXFormAndRedraw.
  void DrawGlyphRunList(const GlyphRunList& glyph_run_list, const PlatformPaint& paint);
  void SimplifyGlyphRunRSXFormAndRedraw(const GlyphRunList& glyph_run_list, const PlatformPaint& paint);

  // SkDraw::paintMasks and SkBitmapDevice::drawBitmap.
  void PaintMasks(std::span<const PlatformGlyph* const> glyphs,
                  std::span<const ScalarPoint> positions,
                  const PlatformPaint& paint) override;
  void DrawBitmap(std::shared_ptr<const Image> image, const ScalarMatrix& matrix, const ScalarRect* dst_or_null,
                  const SamplingOptions& sampling, const PlatformPaint& paint) override;

  // SkDraw::drawSprite: draws the image at device position (x, y).
  void DrawSprite(std::shared_ptr<const Image> image, int x, int y, const PlatformPaint& paint);

  // SkBlitter::blitMask of a kBW, kA8 or kLCD16 mask, limited to clip. The
  // source is the shader context, or solid when there is none.
  void BlitMask(const Mask& mask, const IntRect& clip, const PlatformPaint& paint,
                Shader::Context* context, const PMColor4f& solid);

  Pixmap pixmap_;
  IntRect device_bounds_;
  Layer base_;
  std::vector<State> states_;
  ScratchBuffer* scratch_ = nullptr;
};

} // namespace bkfont
