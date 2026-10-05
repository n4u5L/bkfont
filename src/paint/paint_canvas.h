// Ported from: cc/paint/paint_canvas.h
// Ported from: cc/paint/node_id.h

#pragma once

#include <memory>

#include "matrix.h"
#include "platform_paint.h"
#include "text_blob.h"

namespace bkfont {

class Picture;

// cc::NodeId.
using NodeId = int;
inline constexpr NodeId kInvalidNodeId = 0;

// cc::PaintCanvas, reduced to what text painting uses. CanvasPaintCanvas
// forwards it to a Canvas; cc::PaintFlags is PlatformPaint.
class PaintCanvas {
public:
  virtual ~PaintCanvas() = default;

  virtual int Save() = 0;
  virtual void Restore() = 0;
  virtual int GetSaveCount() const = 0;
  void RestoreToCount(int save_count) {
    if (save_count < 1) {
      save_count = 1;
    }
    int n = GetSaveCount() - save_count;
    for (int i = 0; i < n; ++i) {
      Restore();
    }
  }

  // The matrix is concatenated as an SkM44 made from an SkMatrix.
  virtual void Concat(const ScalarMatrix& matrix) = 0;
  virtual int SaveLayer(const ScalarRect* bounds, const PlatformPaint* paint) = 0;
  virtual void DrawRect(const ScalarRect&, const PlatformPaint&) = 0;
  virtual void DrawLine(ScalarPoint start, ScalarPoint end, const PlatformPaint&) = 0;
  virtual void DrawPath(const ScalarPath&, const PlatformPaint&) = 0;
  virtual void DrawPicture(const Picture&) = 0;
  virtual void ClipRect(const ScalarRect&, bool anti_alias = false) = 0;
  virtual void ClipOutRect(const ScalarRect&, bool anti_alias = false) = 0;
  void Translate(float x, float y) { Concat(ScalarMatrix::Translate(x, y)); }

  virtual void DrawTextBlob(const std::shared_ptr<const TextBlob>& blob, float x, float y, const PlatformPaint& flags) = 0;
  virtual void DrawTextBlob(const std::shared_ptr<const TextBlob>& blob, float x, float y, NodeId node_id,
                            const PlatformPaint& flags) {
    // Node ids only matter to recording canvases.
    (void)node_id;
    DrawTextBlob(blob, x, y, flags);
  }
};

// cc::PaintCanvasAutoRestore.
class PaintCanvasAutoRestore {
public:
  PaintCanvasAutoRestore(PaintCanvas* canvas, bool save)
      : canvas_(canvas) {
    if (canvas_) {
      save_count_ = canvas_->GetSaveCount();
      if (save) {
        canvas_->Save();
      }
    }
  }

  ~PaintCanvasAutoRestore() {
    if (canvas_) {
      canvas_->RestoreToCount(save_count_);
    }
  }

  PaintCanvasAutoRestore(const PaintCanvasAutoRestore&) = delete;
  PaintCanvasAutoRestore& operator=(const PaintCanvasAutoRestore&) = delete;

private:
  PaintCanvas* const canvas_ = nullptr;
  int save_count_ = 0;
};

} // namespace bkfont
