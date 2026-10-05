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
  int SaveLayer(const ScalarRect*, const PlatformPaint*) override;
  void DrawRect(const ScalarRect&, const PlatformPaint&) override;
  void DrawLine(ScalarPoint, ScalarPoint, const PlatformPaint&) override;
  void DrawPath(const ScalarPath&, const PlatformPaint&) override;
  void DrawPicture(const Picture&) override;
  void ClipRect(const ScalarRect&, bool anti_alias = false) override;
  void ClipOutRect(const ScalarRect&, bool anti_alias = false) override;

  using PaintCanvas::DrawTextBlob;
  void DrawTextBlob(const std::shared_ptr<const TextBlob>& blob, float x, float y,
                    const PlatformPaint& flags) override;

private:
  Canvas* const canvas_;
};

} // namespace bkfont
