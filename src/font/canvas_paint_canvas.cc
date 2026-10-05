// Ported from: cc/paint/skia_paint_canvas.cc

#include "canvas_paint_canvas.h"

#include <cassert>

#include "platform/canvas.h"

namespace bkfont {

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
