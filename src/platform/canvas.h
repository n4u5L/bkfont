// Ported from: skia/include/core/SkCanvas.h

#pragma once

#include <memory>

#include "color4f.h"
#include "matrix.h"
#include "path.h"
#include "pixmap.h"
#include "platform_paint.h"
#include "rect.h"
#include "shader.h"

namespace bkfont {

class Drawable;
class GlyphRunBuilder;
class GlyphRunList;
class Picture;
class TextBlob;

// SkCanvas, reduced to the operations that glyph drawing, text blob drawing
// and OpenType SVG decoders use. Paints are always fills; clips are always
// intersections.
class Canvas {
public:
  Canvas();
  virtual ~Canvas();

  virtual int Save() = 0;
  // bounds is in local coordinates and may be null. A null paint composites
  // the layer with kSrcOver at full opacity.
  virtual int SaveLayer(const ScalarRect* bounds, const PlatformPaint* paint) = 0;
  virtual void Restore() = 0;
  virtual int GetSaveCount() const = 0;
  void RestoreToCount(int count);

  virtual void Concat(const ScalarMatrix& matrix) = 0;
  virtual const ScalarMatrix& GetTotalMatrix() const = 0;
  void Translate(float dx, float dy);
  void Scale(float sx, float sy);

  virtual void ClipRect(const ScalarRect& rect, bool do_anti_alias) = 0;
  virtual void ClipPath(const ScalarPath& path, bool do_anti_alias) = 0;

  // Fills the clip.
  virtual void DrawPaint(const PlatformPaint& paint) = 0;
  virtual void DrawPath(const ScalarPath& path, const PlatformPaint& paint) = 0;
  void DrawRect(const ScalarRect& rect, const PlatformPaint& paint);
  // SkCanvas::drawImage: the image is drawn at (x, y) with kFast_SrcRectConstraint.
  virtual void DrawImage(std::shared_ptr<const Image> image, float x, float y,
                         const SamplingOptions& sampling, const PlatformPaint* paint) = 0;
  // SkCanvas::drawColor.
  void DrawColor(const Color4f& color, BlendMode mode = BlendMode::kSrcOver);
  // SkCanvas::clear: drawColor with kSrc.
  void Clear(ColorARGB color);

  // Plays the picture back inside a save/restore pair.
  virtual void DrawPicture(const Picture& picture);
  // SkCanvas::drawDrawable. A recording canvas retains the drawable, which
  // must be owned by a shared_ptr for the duration of this call.
  virtual void DrawDrawable(Drawable* drawable, const ScalarMatrix* matrix = nullptr);

  // SkCanvas::drawTextBlob. Nothing is drawn for a null blob, for bounds
  // that are not finite at (x, y), or for more than 2^21 glyphs.
  void DrawTextBlob(const std::shared_ptr<const TextBlob>& blob, float x, float y, const PlatformPaint& paint);

protected:
  // SkCanvas::onDrawTextBlob: converts the blob to glyph runs at (x, y).
  virtual void OnDrawTextBlob(const std::shared_ptr<const TextBlob>& blob, float x, float y,
                              const PlatformPaint& paint);
  virtual void OnDrawGlyphRunList(const GlyphRunList& glyph_run_list, const PlatformPaint& paint) = 0;

private:
  // SkCanvas::fScratchGlyphRunBuilder.
  std::unique_ptr<GlyphRunBuilder> scratch_glyph_run_builder_;
};

// SkAutoCanvasRestore.
class AutoCanvasRestore {
public:
  AutoCanvasRestore(Canvas* canvas, bool do_save)
      : canvas_(canvas) {
    if (canvas_) {
      save_count_ = canvas_->GetSaveCount();
      if (do_save) {
        canvas_->Save();
      }
    }
  }
  ~AutoCanvasRestore() {
    if (canvas_) {
      canvas_->RestoreToCount(save_count_);
    }
  }
  AutoCanvasRestore(const AutoCanvasRestore&) = delete;
  AutoCanvasRestore& operator=(const AutoCanvasRestore&) = delete;

private:
  Canvas* canvas_;
  int save_count_ = 0;
};

} // namespace bkfont
