// Ported from: skia/src/core/SkCanvas.cpp
// Ported from: skia/src/core/SkDrawable.cpp

#include "canvas.h"

#include <cstdint>
#include <utility>

#include "glyph_run.h"
#include "picture.h"
#include "text_blob.h"

namespace bkit {

Canvas::Canvas()
    : scratch_glyph_run_builder_(std::make_unique<GlyphRunBuilder>()) {
}

Canvas::~Canvas() = default;

void Canvas::RestoreToCount(int count) {
  // sanity check
  if (count < 1) {
    count = 1;
  }

  int n = GetSaveCount() - count;
  for (int i = 0; i < n; ++i) {
    Restore();
  }
}

void Canvas::Translate(float dx, float dy) {
  if (dx || dy) {
    Concat(ScalarMatrix::Translate(dx, dy));
  }
}

void Canvas::Scale(float sx, float sy) {
  if (sx != 1 || sy != 1) {
    Concat(ScalarMatrix::Scale(sx, sy));
  }
}

void Canvas::DrawRect(const ScalarRect& rect, const PlatformPaint& paint) {
  DrawPath(ScalarPath::Rect(rect), paint);
}

void Canvas::DrawLine(ScalarPoint start, ScalarPoint end, const PlatformPaint& paint) {
  ScalarPath path;
  path.MoveTo(start);
  path.LineTo(end);
  PlatformPaint stroke = paint;
  stroke.SetStyle(PlatformPaint::Style::kStroke);
  DrawPath(path, stroke);
}

void Canvas::DrawColor(const Color4f& color, BlendMode mode) {
  PlatformPaint paint;
  paint.SetColor(color);
  paint.SetBlendMode(mode);
  DrawPaint(paint);
}

void Canvas::Clear(ColorARGB color) {
  DrawColor(Color4f::FromColor(color), BlendMode::kSrc);
}

void Canvas::DrawPicture(const Picture& picture) {
  AutoCanvasRestore acr(this, true);
  picture.Playback(this);
}

void Canvas::DrawDrawable(Drawable* drawable, const ScalarMatrix* matrix) {
  if (!drawable) {
    return;
  }
  drawable->Draw(this, matrix);
}

void Canvas::DrawTextBlob(const std::shared_ptr<const TextBlob>& blob, float x, float y, const PlatformPaint& paint) {
  if (!blob) {
    return;
  }
  // SkRect::isFinite of blob->bounds().makeOffset(x, y).
  ScalarRect bounds = blob->Bounds();
  bounds.Offset(x, y);
  float accum = 0;
  accum *= bounds.left;
  accum *= bounds.top;
  accum *= bounds.right;
  accum *= bounds.bottom;
  if (accum != 0) {
    return;
  }

  // Overflow if more than 2^21 glyphs stopping a buffer overflow latter in
  // the stack. See chromium:1080481
  // TODO: can consider unrolling a few at a time if this limit becomes a
  // problem.
  int total_glyph_count = 0;
  constexpr int kMaxGlyphCount = 1 << 21;
  for (TextBlobRunIterator it(blob.get()); !it.Done(); it.Next()) {
    int glyphs_left = kMaxGlyphCount - total_glyph_count;
    if (!(static_cast<std::int64_t>(it.GlyphCount()) <= glyphs_left)) {
      return;
    }
    total_glyph_count += static_cast<int>(it.GlyphCount());
  }

  OnDrawTextBlob(blob, x, y, paint);
}

void Canvas::OnDrawTextBlob(const std::shared_ptr<const TextBlob>& blob, float x, float y,
                            const PlatformPaint& paint) {
  const GlyphRunList& glyph_run_list = scratch_glyph_run_builder_->BlobToGlyphRunList(*blob, {x, y});
  OnDrawGlyphRunList(glyph_run_list, paint);
}

} // namespace bkit
