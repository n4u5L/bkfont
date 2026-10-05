// Ported from: cc/paint/skia_paint_canvas.cc

#include "canvas_paint_canvas.h"

#include <cassert>

#include "canvas.h"

namespace bkfont {

int CanvasPaintCanvas::SaveLayer(const ScalarRect* bounds, const PlatformPaint* paint) {
  return canvas_->SaveLayer(bounds, paint);
}
void CanvasPaintCanvas::DrawRect(const ScalarRect& rect, const PlatformPaint& paint) {
  canvas_->DrawRect(rect, paint);
}
void CanvasPaintCanvas::DrawLine(ScalarPoint start, ScalarPoint end, const PlatformPaint& paint) {
  canvas_->DrawLine(start, end, paint);
}
void CanvasPaintCanvas::DrawPath(const ScalarPath& path, const PlatformPaint& paint) {
  canvas_->DrawPath(path, paint);
}
void CanvasPaintCanvas::DrawPicture(const Picture& picture) { canvas_->DrawPicture(picture); }
void CanvasPaintCanvas::ClipRect(const ScalarRect& rect, bool aa) { canvas_->ClipRect(rect, aa); }
void CanvasPaintCanvas::ClipOutRect(const ScalarRect& rect, bool aa) { canvas_->ClipOutRect(rect, aa); }

CanvasPaintCanvas::CanvasPaintCanvas(Canvas* canvas)
    : canvas_(canvas) {
  assert(canvas_);
}

int CanvasPaintCanvas::Save() {
  return canvas_->Save();
}

void CanvasPaintCanvas::Restore() {
  canvas_->Restore();
}

int CanvasPaintCanvas::GetSaveCount() const {
  return canvas_->GetSaveCount();
}

void CanvasPaintCanvas::Concat(const ScalarMatrix& matrix) {
  canvas_->Concat(matrix);
}

void CanvasPaintCanvas::DrawTextBlob(const std::shared_ptr<const TextBlob>& blob, float x, float y,
                                    const PlatformPaint& flags) {
  canvas_->DrawTextBlob(blob, x, y, flags);
}

} // namespace bkfont
