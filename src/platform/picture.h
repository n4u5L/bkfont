// Ported from: skia/include/core/SkPicture.h
// Ported from: skia/include/core/SkPictureRecorder.h
// Ported from: skia/include/core/SkDrawable.h
// Ported from: skia/src/core/SkRecordedDrawable.h

#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "canvas.h"

namespace bkfont {

// SkPicture: a recorded list of canvas operations.
class Picture {
public:
  struct Op;

  Picture(std::vector<Op> ops, const ScalarRect& cull_rect);
  ~Picture();

  void Playback(Canvas* canvas) const;

  // The cull rect given to the recorder, or the bounds of the recorded
  // content when the recorder computed them.
  const ScalarRect& CullRect() const {
    return cull_rect_;
  }

  std::size_t ApproximateBytesUsed() const;

private:
  std::vector<Op> ops_;
  ScalarRect cull_rect_;
};

// SkDrawable.
class Drawable : public std::enable_shared_from_this<Drawable> {
public:
  virtual ~Drawable();

  // Draws inside a save/restore pair, after concatenating matrix if any.
  void Draw(Canvas* canvas, const ScalarMatrix* matrix = nullptr);

  // Freezes the current drawing, including any nested drawables.
  std::shared_ptr<const Picture> MakePictureSnapshot();

  ScalarRect GetBounds() {
    return OnGetBounds();
  }
  std::size_t ApproximateBytesUsed() {
    return OnApproximateBytesUsed();
  }

protected:
  virtual void OnDraw(Canvas* canvas) = 0;
  virtual ScalarRect OnGetBounds() = 0;
  virtual std::shared_ptr<const Picture> OnMakePictureSnapshot();
  virtual std::size_t OnApproximateBytesUsed() {
    return 0;
  }
};

// SkPictureRecorder. With compute_bounds, the cull rect of the finished
// picture becomes the bounds of its content, as recording with an SkRTree
// bounding box hierarchy does.
class PictureRecorder {
public:
  PictureRecorder();
  ~PictureRecorder();
  PictureRecorder(const PictureRecorder&) = delete;
  PictureRecorder& operator=(const PictureRecorder&) = delete;

  Canvas* BeginRecording(const ScalarRect& cull_rect, bool compute_bounds = false);
  Canvas* GetRecordingCanvas();

  std::shared_ptr<const Picture> FinishRecordingAsPicture();
  // SkRecordedDrawable, whose bounds are the cull rect. Nested drawables are
  // retained and their current state is used on every playback.
  std::shared_ptr<Drawable> FinishRecordingAsDrawable();

private:
  class RecordingCanvas;
  std::unique_ptr<RecordingCanvas> canvas_;
};

} // namespace bkfont
