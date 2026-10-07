// Ported from: skia/src/core/SkPictureRecorder.cpp
// Ported from: skia/src/core/SkRecordCanvas.cpp
// Ported from: skia/src/core/SkRecordedDrawable.cpp
// Ported from: skia/src/core/SkRecordDraw.cpp
// Ported from: skia/src/core/SkDrawable.cpp

#include "picture.h"

#include <algorithm>
#include <utility>

#include "glyph_run.h"
#include "path_effect.h"
#include "text_blob.h"

namespace bkit {

struct Picture::Op {
  enum class Type {
    kSave,
    kSaveLayer,
    kRestore,
    kConcat,
    kClipRect,
    kClipPath,
    kClipOutRect,
    kClipOutPath,
    kDrawPaint,
    kDrawPath,
    kDrawImage,
    kDrawDrawable,
    kDrawPicture,
    kDrawTextBlob,
  };

  Type type = Type::kSave;
  ScalarMatrix matrix;
  ScalarRect rect;
  bool has_rect = false;
  ScalarPath path;
  bool anti_alias = false;
  PlatformPaint paint;
  bool has_paint = false;
  std::shared_ptr<const Image> image;
  std::shared_ptr<Drawable> drawable;
  std::shared_ptr<const Picture> picture;
  std::shared_ptr<const TextBlob> blob;
  float x = 0;
  float y = 0;
  SamplingOptions sampling;
};

Picture::Picture(std::vector<Op> ops, const ScalarRect& cull_rect)
    : ops_(std::move(ops)), cull_rect_(cull_rect) {
}

Picture::~Picture() = default;

namespace {

void PlaybackOps(const std::vector<Picture::Op>& ops, Canvas* canvas) {
  // SkRecordDraw saves now and restores at exit.
  AutoCanvasRestore save_restore(canvas, true);
  using Op = Picture::Op;
  for (const Op& op : ops) {
    switch (op.type) {
    case Op::Type::kSave:
      canvas->Save();
      break;
    case Op::Type::kSaveLayer:
      canvas->SaveLayer(op.has_rect ? &op.rect : nullptr, op.has_paint ? &op.paint : nullptr);
      break;
    case Op::Type::kRestore:
      canvas->Restore();
      break;
    case Op::Type::kConcat:
      canvas->Concat(op.matrix);
      break;
    case Op::Type::kClipRect:
      canvas->ClipRect(op.rect, op.anti_alias);
      break;
    case Op::Type::kClipPath:
      canvas->ClipPath(op.path, op.anti_alias);
      break;
    case Op::Type::kClipOutRect:
      canvas->ClipOutRect(op.rect, op.anti_alias);
      break;
    case Op::Type::kClipOutPath:
      canvas->ClipOutPath(op.path, op.anti_alias);
      break;
    case Op::Type::kDrawPaint:
      canvas->DrawPaint(op.paint);
      break;
    case Op::Type::kDrawPath:
      canvas->DrawPath(op.path, op.paint);
      break;
    case Op::Type::kDrawImage:
      canvas->DrawImage(op.image, op.x, op.y, op.sampling, op.has_paint ? &op.paint : nullptr);
      break;
    case Op::Type::kDrawDrawable:
      canvas->DrawDrawable(op.drawable.get(), &op.matrix);
      break;
    case Op::Type::kDrawPicture: {
      AutoCanvasRestore acr(canvas, true);
      canvas->Concat(op.matrix);
      canvas->DrawPicture(*op.picture);
      break;
    }
    case Op::Type::kDrawTextBlob:
      canvas->DrawTextBlob(op.blob, op.x, op.y, op.paint);
      break;
    }
  }
}

std::size_t OpsBytesUsed(const std::vector<Picture::Op>& ops) {
  std::size_t bytes = ops.capacity() * sizeof(Picture::Op);
  for (const Picture::Op& op : ops) {
    if (!op.path.IsEmpty()) {
      bytes += op.path.ApproximateBytesUsed() - sizeof(ScalarPath);
    }
    if (op.image) {
      bytes += static_cast<std::size_t>(op.image->GetPixmap().RowBytes()) * static_cast<std::size_t>(op.image->Height());
    }
    if (op.drawable) {
      bytes += op.drawable->ApproximateBytesUsed();
    }
    if (op.picture) {
      bytes += op.picture->ApproximateBytesUsed();
    }
  }
  return bytes;
}

// SkDrawableList::newDrawableSnapshot: freeze children when a picture is
// finished, not when the drawDrawable operation was originally recorded.
void SnapshotDrawables(std::vector<Picture::Op>* ops) {
  for (Picture::Op& op : *ops) {
    if (op.type == Picture::Op::Type::kDrawDrawable) {
      op.picture = op.drawable->MakePictureSnapshot();
      op.drawable.reset();
      op.type = Picture::Op::Type::kDrawPicture;
    }
  }
}

} // namespace

void Picture::Playback(Canvas* canvas) const {
  PlaybackOps(ops_, canvas);
}

std::size_t Picture::ApproximateBytesUsed() const {
  return sizeof(*this) + OpsBytesUsed(ops_);
}

Drawable::~Drawable() = default;

void Drawable::Draw(Canvas* canvas, const ScalarMatrix* matrix) {
  AutoCanvasRestore acr(canvas, true);
  if (matrix) {
    canvas->Concat(*matrix);
  }
  OnDraw(canvas);
}

std::shared_ptr<const Picture> Drawable::MakePictureSnapshot() {
  return OnMakePictureSnapshot();
}

std::shared_ptr<const Picture> Drawable::OnMakePictureSnapshot() {
  PictureRecorder recorder;
  Draw(recorder.BeginRecording(GetBounds()));
  return recorder.FinishRecordingAsPicture();
}

namespace {

// SkRecordedDrawable.
class RecordedDrawable final : public Drawable {
public:
  RecordedDrawable(std::vector<Picture::Op> ops, const ScalarRect& bounds)
      : ops_(std::move(ops)), bounds_(bounds) {
  }

protected:
  void OnDraw(Canvas* canvas) override {
    PlaybackOps(ops_, canvas);
  }
  ScalarRect OnGetBounds() override {
    return bounds_;
  }
  std::size_t OnApproximateBytesUsed() override {
    return sizeof(*this) + OpsBytesUsed(ops_);
  }
  std::shared_ptr<const Picture> OnMakePictureSnapshot() override {
    std::vector<Picture::Op> snapshots = ops_;
    SnapshotDrawables(&snapshots);
    return std::make_shared<Picture>(std::move(snapshots), bounds_);
  }

private:
  std::vector<Picture::Op> ops_;
  ScalarRect bounds_;
};

ScalarRect IntersectRects(const ScalarRect& a, const ScalarRect& b) {
  ScalarRect r = ScalarRect::MakeLTRB(std::max(a.left, b.left), std::max(a.top, b.top),
                                      std::min(a.right, b.right), std::min(a.bottom, b.bottom));
  if (r.IsEmpty()) {
    return ScalarRect();
  }
  return r;
}

} // namespace

// SkRecorder. Records the operations and, when asked to, the bounds of the
// content in the cull rect's space as SkRecordFillBounds approximates them:
// draws are bounded by their geometry and the recording cull rect. Clips do
// not shrink these conservative bounds; unbounded draws and layers whose
// blend affects transparent black use the entire recording cull rect.
class PictureRecorder::RecordingCanvas final : public Canvas {
public:
  RecordingCanvas(const ScalarRect& cull_rect, bool compute_bounds)
      : cull_rect_(cull_rect), compute_bounds_(compute_bounds) {
    states_.emplace_back();
  }

  int Save() override {
    const int previous = GetSaveCount();
    Picture::Op op;
    op.type = Picture::Op::Type::kSave;
    ops_.push_back(std::move(op));
    State state = states_.back();
    state.layer_affects_transparent_black = false;
    states_.push_back(state);
    return previous;
  }

  int SaveLayer(const ScalarRect* bounds, const PlatformPaint* paint) override {
    const int previous = GetSaveCount();
    Picture::Op op;
    op.type = Picture::Op::Type::kSaveLayer;
    if (bounds) {
      op.rect = *bounds;
      op.has_rect = true;
    }
    if (paint) {
      op.paint = *paint;
      op.has_paint = true;
    }
    ops_.push_back(std::move(op));
    State state = states_.back();
    state.layer_affects_transparent_black = paint &&
        (BlendModeAffectsTransparentBlack(paint->GetBlendMode()) || paint->GetImageFilter());
    states_.push_back(state);
    return previous;
  }

  void Restore() override {
    if (states_.size() <= 1) {
      return;
    }
    Picture::Op op;
    op.type = Picture::Op::Type::kRestore;
    ops_.push_back(std::move(op));
    const State state = states_.back();
    states_.pop_back();
    if (state.layer_affects_transparent_black) {
      AddBounds(cull_rect_);
    }
  }

  int GetSaveCount() const override {
    return static_cast<int>(states_.size());
  }

  void Concat(const ScalarMatrix& matrix) override {
    Picture::Op op;
    op.type = Picture::Op::Type::kConcat;
    op.matrix = matrix;
    ops_.push_back(std::move(op));
    states_.back().matrix.PreConcat(matrix);
  }

  const ScalarMatrix& GetTotalMatrix() const override {
    return states_.back().matrix;
  }

  void ClipRect(const ScalarRect& rect, bool do_anti_alias) override {
    Picture::Op op;
    op.type = Picture::Op::Type::kClipRect;
    op.rect = rect;
    op.anti_alias = do_anti_alias;
    ops_.push_back(std::move(op));
  }

  void ClipPath(const ScalarPath& path, bool do_anti_alias) override {
    Picture::Op op;
    op.type = Picture::Op::Type::kClipPath;
    op.path = path;
    op.anti_alias = do_anti_alias;
    ops_.push_back(std::move(op));
  }

  void DrawPaint(const PlatformPaint& paint) override {
    Picture::Op op;
    op.type = Picture::Op::Type::kDrawPaint;
    op.paint = paint;
    ops_.push_back(std::move(op));
    AddBounds(cull_rect_);
  }

  void ClipOutRect(const ScalarRect& rect, bool aa) override {
    Picture::Op op;
    op.type = Picture::Op::Type::kClipOutRect;
    op.rect = rect;
    op.anti_alias = aa;
    ops_.push_back(std::move(op));
  }
  void ClipOutPath(const ScalarPath& path, bool aa) override {
    Picture::Op op;
    op.type = Picture::Op::Type::kClipOutPath;
    op.path = path;
    op.anti_alias = aa;
    ops_.push_back(std::move(op));
  }

  void DrawPath(const ScalarPath& path, const PlatformPaint& paint) override {
    Picture::Op op;
    op.type = Picture::Op::Type::kDrawPath;
    op.path = path;
    op.paint = paint;
    ops_.push_back(std::move(op));
    if (paint.GetPathEffect()) {
      ScalarPath fill_path;
      const bool fill = FillPathWithPaint(path, paint, &fill_path, nullptr, states_.back().matrix);
      ScalarRect bounds = fill_path.GetBounds();
      if (fill) AddLocalBounds(bounds);
      else AddHairlineBounds(bounds);
    } else {
      AddPaintBounds(path.GetBounds(), paint);
    }
  }

  void DrawImage(std::shared_ptr<const Image> image, float x, float y,
                 const SamplingOptions& sampling, const PlatformPaint* paint) override {
    if (!image) {
      return;
    }
    const ScalarRect dst = ScalarRect::MakeXYWH(x, y, static_cast<float>(image->Width()), static_cast<float>(image->Height()));
    Picture::Op op;
    op.type = Picture::Op::Type::kDrawImage;
    op.image = std::move(image);
    op.x = x;
    op.y = y;
    op.sampling = sampling;
    if (paint) {
      op.paint = *paint;
      op.has_paint = true;
    }
    ops_.push_back(std::move(op));
    AddLocalBounds(dst);
  }

  void DrawDrawable(Drawable* drawable, const ScalarMatrix* matrix) override {
    if (!drawable) {
      return;
    }
    Picture::Op op;
    op.type = Picture::Op::Type::kDrawDrawable;
    op.drawable = drawable->shared_from_this();
    if (matrix) {
      op.matrix = *matrix;
    }
    // SkRecordFillBounds uses the recorded worstCaseBounds and the current
    // CTM, without applying the optional per-drawable matrix.
    const ScalarRect bounds = drawable->GetBounds();
    ops_.push_back(std::move(op));
    AddLocalBounds(bounds);
  }

protected:
  void OnDrawTextBlob(const std::shared_ptr<const TextBlob>& blob, float x, float y,
                      const PlatformPaint& paint) override {
    Picture::Op op;
    op.type = Picture::Op::Type::kDrawTextBlob;
    op.blob = blob;
    op.x = x;
    op.y = y;
    op.paint = paint;
    ops_.push_back(std::move(op));
    ScalarRect dst = blob->Bounds();
    dst.Offset(x, y);
    AddPaintBounds(dst, paint);
  }

  // The blob of a glyph run list is not shared, so the runs are always
  // copied into a new blob.
  void OnDrawGlyphRunList(const GlyphRunList& glyph_run_list, const PlatformPaint& paint) override {
    std::shared_ptr<const TextBlob> blob = glyph_run_list.MakeBlob();
    if (!blob) {
      return;
    }
    OnDrawTextBlob(blob, glyph_run_list.Origin().x, glyph_run_list.Origin().y, paint);
  }

public:
  std::shared_ptr<const Picture> FinishPicture() {
    RestoreToCount(1);
    if (ops_.empty()) {
      return std::make_shared<Picture>(std::vector<Picture::Op>(), ScalarRect());
    }
    SnapshotDrawables(&ops_);
    ScalarRect cull = cull_rect_;
    if (compute_bounds_) {
      // Now that we've calculated content bounds, we can update the cull
      // rect, often trimming it.
      cull = has_content_bounds_ ? content_bounds_ : ScalarRect();
    }
    return std::make_shared<Picture>(std::move(ops_), cull);
  }

  std::shared_ptr<Drawable> FinishDrawable() {
    RestoreToCount(1);
    return std::make_shared<RecordedDrawable>(std::move(ops_), cull_rect_);
  }

private:
  struct State {
    ScalarMatrix matrix;
    bool layer_affects_transparent_black = false;
  };

  void AddLocalBounds(const ScalarRect& local) {
    ScalarRect mapped = local;
    states_.back().matrix.MapRect(&mapped);
    AddBounds(IntersectRects(mapped, cull_rect_));
  }

  void AddPaintBounds(ScalarRect bounds, const PlatformPaint& paint) {
    if (paint.GetPathEffect() || paint.GetImageFilter()) {
      AddBounds(cull_rect_);
      return;
    }
    if (paint.GetStyle() == PlatformPaint::Style::kStroke && paint.GetStrokeWidth() == 0) {
      AddHairlineBounds(bounds);
      return;
    }
    const float radius = StrokeRec(paint).GetInflationRadius();
    bounds.Outset(radius, radius);
    AddLocalBounds(bounds);
  }

  void AddHairlineBounds(ScalarRect bounds) {
    states_.back().matrix.MapRect(&bounds);
    bounds.Outset(1, 1); // Hairlines have device-space thickness.
    AddBounds(IntersectRects(bounds, cull_rect_));
  }

  void AddBounds(const ScalarRect& bounds) {
    if (!compute_bounds_ || bounds.IsEmpty()) {
      return;
    }
    if (!has_content_bounds_) {
      content_bounds_ = bounds;
      has_content_bounds_ = true;
    } else {
      content_bounds_.Join(bounds);
    }
  }

  ScalarRect cull_rect_;
  bool compute_bounds_;
  std::vector<Picture::Op> ops_;
  std::vector<State> states_;
  ScalarRect content_bounds_;
  bool has_content_bounds_ = false;
};

PictureRecorder::PictureRecorder() = default;
PictureRecorder::~PictureRecorder() = default;

Canvas* PictureRecorder::BeginRecording(const ScalarRect& cull_rect, bool compute_bounds) {
  canvas_ = std::make_unique<RecordingCanvas>(cull_rect.IsEmpty() ? ScalarRect() : cull_rect, compute_bounds);
  return canvas_.get();
}

Canvas* PictureRecorder::GetRecordingCanvas() {
  return canvas_.get();
}

std::shared_ptr<const Picture> PictureRecorder::FinishRecordingAsPicture() {
  if (!canvas_) {
    return std::make_shared<Picture>(std::vector<Picture::Op>(), ScalarRect());
  }
  std::shared_ptr<const Picture> picture = canvas_->FinishPicture();
  canvas_.reset();
  return picture;
}

std::shared_ptr<Drawable> PictureRecorder::FinishRecordingAsDrawable() {
  if (!canvas_) {
    return nullptr;
  }
  std::shared_ptr<Drawable> drawable = canvas_->FinishDrawable();
  canvas_.reset();
  return drawable;
}

} // namespace bkit
