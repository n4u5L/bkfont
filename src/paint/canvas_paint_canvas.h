// Ported from: cc/paint/skia_paint_canvas.h

#pragma once

#include "paint_canvas.h"

namespace bkfont {

class Canvas;

// The text-painting subset of cc::SkiaPaintCanvas, forwarding to a raster or
// recording Canvas. The canvas must outlive this non-owning adapter.
class CanvasPaintCanvas final : public PaintCanvas {
public:
  explicit CanvasPaintCanvas(Canvas* canvas);
  CanvasPaintCanvas(const CanvasPaintCanvas&) = delete;
  CanvasPaintCanvas& operator=(const CanvasPaintCanvas&) = delete;

  int Save() override;
  void Restore() override;
  int GetSaveCount() const override;
  void Concat(const ScalarMatrix& matrix) override;

  using PaintCanvas::DrawTextBlob;
  void DrawTextBlob(const std::shared_ptr<const TextBlob>& blob, float x, float y,
                    const PlatformPaint& flags) override;

private:
  Canvas* const canvas_;
};

} // namespace bkfont
