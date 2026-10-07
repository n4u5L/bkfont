// Ported from: skia/include/core/SkPathBuilder.h
// Ported from: skia/src/core/SkPathBuilder.cpp
// Ported from: skia/src/core/SkPath.cpp (Iter, isRect, isLine)
// Ported from: skia/src/core/SkPathPriv.cpp (IsRectContour)
// Ported from: skia/src/core/SkPathRawShapes.cpp
// Ported from: skia/src/core/SkPathMakers.h

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "matrix.h"

namespace bkit {

// SkPathDirection.
enum class PathDirection : std::uint8_t {
  kCW,
  kCCW,
};

// SkPathBuilder with the SkPath queries the stroker and dasher use. Bounds
// include control points, as SkPath::getBounds does, rather than curve
// extrema. Transform the points before measuring to avoid inflating bounds.
// There is no convexity, first-direction or oval/rrect tracking.
class ScalarPath {
public:
  // SkPathVerb.
  enum class Verb : std::uint8_t {
    kMove,
    kLine,
    kQuad,
    kConic,
    kCubic,
    kClose
  };

  // SkPathFillType. Glyph outlines use the default winding rule.
  enum class FillType : std::uint8_t {
    kWinding,
    kEvenOdd,
  };

  FillType GetFillType() const {
    return fill_type_;
  }
  void SetFillType(FillType fill_type) {
    fill_type_ = fill_type;
  }

  bool IsEmpty() const {
    return verbs_.empty();
  }
  int CountPoints() const {
    return static_cast<int>(points_.size());
  }

  // SkPath::Rect, clockwise from the top left.
  static ScalarPath Rect(const ScalarRect& rect);
  // SkPath::Polygon.
  static ScalarPath Polygon(std::span<const ScalarPoint> points, bool is_closed);

  void Reset();
  void MoveTo(ScalarPoint point);
  void LineTo(ScalarPoint point);
  void QuadTo(ScalarPoint control, ScalarPoint end);
  void ConicTo(ScalarPoint control, ScalarPoint end, float weight);
  void CubicTo(ScalarPoint control1, ScalarPoint control2, ScalarPoint end);
  void Close();

  // SkPathBuilder::addRect/addOval/addCircle/addRRect/addPolygon. The rrect
  // has the same radii at every corner (SkRRect::MakeRectXY).
  void AddRect(const ScalarRect& rect, PathDirection dir = PathDirection::kCW, unsigned start_index = 0);
  void AddOval(const ScalarRect& oval, PathDirection dir = PathDirection::kCW, unsigned start_index = 1);
  void AddCircle(float x, float y, float radius, PathDirection dir = PathDirection::kCW);
  void AddRRect(const ScalarRect& rect, float x_radius, float y_radius, PathDirection dir = PathDirection::kCW,
                unsigned start_index = 6);
  void AddPolygon(std::span<const ScalarPoint> points, bool is_closed);
  // SkPathBuilder::addPath in kAppend_AddPathMode with the identity matrix.
  void AddPath(const ScalarPath& src);
  // SkPathBuilder::privateReversePathTo: appends the last contour of `path`
  // reversed, ignoring its last point.
  void ReversePathTo(const ScalarPath& path);

  std::optional<ScalarPoint> GetLastPt() const;
  void SetLastPt(ScalarPoint point);

  void Transform(const ScalarMatrix& matrix);
  ScalarRect GetBounds() const;
  // SkPath::computeTightBounds, including curve extrema rather than controls.
  ScalarRect ComputeTightBounds() const;
  // SkPathBuilder::computeBounds: the bounds of the points, finite or not.
  ScalarRect ComputeBounds() const;
  bool IsFinite() const;

  // SkPath::isRect for the first contour.
  bool IsRect(ScalarRect* rect, bool* is_closed = nullptr, PathDirection* direction = nullptr) const;
  // SkPath::isLine.
  bool IsLine(ScalarPoint line[2]) const;
  bool IsLastContourClosed() const {
    return !verbs_.empty() && verbs_.back() == Verb::kClose;
  }

  // SkPath::makeOffset.
  ScalarPath MakeOffset(float dx, float dy) const;

  // SkPath::approximateBytesUsed: the object plus its point and verb storage.
  std::size_t ApproximateBytesUsed() const;

  std::span<const ScalarPoint> Points() const {
    return points_;
  }
  std::span<const Verb> Verbs() const {
    return verbs_;
  }
  std::span<const float> ConicWeights() const {
    return conic_weights_;
  }

  static int PtsInVerb(Verb verb);

  // SkPath::Iter: each segment with its start point; with `force_close`,
  // open contours get a closing line and a close. A close is preceded by a
  // closing line when the last point is not the move point.
  class Iter {
  public:
    Iter(const ScalarPath& path, bool force_close);
    // The verb, or nullopt when done. `pts` receives up to four points; for a
    // conic, `ConicWeight()` is its weight.
    std::optional<Verb> Next(ScalarPoint pts[4]);
    float ConicWeight() const {
      return conic_weight_;
    }
    // Whether the last line returned was the closing line of a close.
    bool IsCloseLine() const {
      return close_line_;
    }

  private:
    Verb AutoClose(ScalarPoint pts[2]);

    const ScalarPath* path_;
    std::size_t verb_index_ = 0;
    std::size_t point_index_ = 0;
    std::size_t weight_index_ = 0;
    ScalarPoint move_to_;
    ScalarPoint last_pt_;
    float conic_weight_ = 1;
    bool force_close_;
    bool need_close_ = false;
    bool close_line_ = false;
  };

private:
  void EnsureMove();

  std::vector<ScalarPoint> points_;
  std::vector<Verb> verbs_;
  std::vector<float> conic_weights_;
  ScalarPoint last_move_;
  bool needs_move_ = true;
  FillType fill_type_ = FillType::kWinding;
};

} // namespace bkit
