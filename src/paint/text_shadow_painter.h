// Ported from: blink/renderer/core/paint/text_shadow_painter.h
#pragma once
#include "canvas_paint_canvas.h"
#include "paint_canvas.h"
#include "picture.h"
#include "text_paint_style.h"

namespace bkfont {
std::shared_ptr<const ImageFilter> MakeTextShadowFilter(const TextPaintStyle&);

template <typename PaintProc>
void PaintWithTextShadow(PaintCanvas* canvas, const TextPaintStyle& style, PaintProc paint) {
  if (auto filter = MakeTextShadowFilter(style)) {
    // Record the shadow source once to obtain bounds for glyphs, emphasis and
    // decorations, including their transforms and stroke outsets. The bounds
    // are a content hint, not the output clip: the filter can still reach into
    // the saved canvas clip. Empty phases allocate no pixel layer at all.
    PictureRecorder recorder;
    constexpr float limit = static_cast<float>(1 << 29);
    CanvasPaintCanvas recording(recorder.BeginRecording({-limit, -limit, limit, limit}, true));
    paint(&recording, true);
    const auto picture = recorder.FinishRecordingAsPicture();
    if (!picture->CullRect().IsEmpty()) {
      PaintCanvasAutoRestore restore(canvas, false);
      PlatformPaint layer;
      layer.SetImageFilter(std::move(filter));
      canvas->SaveLayer(&picture->CullRect(), &layer);
      canvas->DrawPicture(*picture);
    }
  }
  paint(canvas, false);
}
} // namespace bkfont
