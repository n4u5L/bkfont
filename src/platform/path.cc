// Ported from: skia/src/core/SkPathBuilder.cpp

#include "path.h"

#include <cmath>

namespace bkfont {

void ScalarPath::EnsureMove() {
  if (needs_move_) MoveTo(last_move_);
}

void ScalarPath::MoveTo(ScalarPoint point) {
  points_.push_back(point);
  verbs_.push_back(Verb::kMove);
  last_move_ = point;
  needs_move_ = false;
}

void ScalarPath::LineTo(ScalarPoint point) {
  EnsureMove();
  points_.push_back(point);
  verbs_.push_back(Verb::kLine);
}

void ScalarPath::QuadTo(ScalarPoint control, ScalarPoint end) {
  EnsureMove();
  points_.push_back(control);
  points_.push_back(end);
  verbs_.push_back(Verb::kQuad);
}

void ScalarPath::CubicTo(ScalarPoint control1, ScalarPoint control2, ScalarPoint end) {
  EnsureMove();
  points_.push_back(control1);
  points_.push_back(control2);
  points_.push_back(end);
  verbs_.push_back(Verb::kCubic);
}

void ScalarPath::Close() {
  if (!verbs_.empty() && verbs_.back() != Verb::kClose) {
    verbs_.push_back(Verb::kClose);
    needs_move_ = true;
  }
}

void ScalarPath::Transform(const ScalarMatrix& matrix) {
  if (matrix.IsIdentity()) return;
  for (ScalarPoint& point : points_) point = matrix.MapPoint(point);
}

ScalarPath ScalarPath::Rect(const ScalarRect& rect) {
  ScalarPath path;
  path.MoveTo({rect.left, rect.top});
  path.LineTo({rect.right, rect.top});
  path.LineTo({rect.right, rect.bottom});
  path.LineTo({rect.left, rect.bottom});
  path.Close();
  return path;
}

ScalarPath ScalarPath::Polygon(std::span<const ScalarPoint> points, bool is_closed) {
  ScalarPath path;
  if (points.empty()) {
    return path;
  }
  path.MoveTo(points[0]);
  for (std::size_t i = 1; i < points.size(); ++i) {
    path.LineTo(points[i]);
  }
  if (is_closed) {
    path.Close();
  }
  return path;
}

ScalarPath ScalarPath::MakeOffset(float dx, float dy) const {
  ScalarPath result = *this;
  result.Transform(ScalarMatrix::Translate(dx, dy));
  return result;
}

std::size_t ScalarPath::ApproximateBytesUsed() const {
  return sizeof(ScalarPath) + points_.capacity() * sizeof(ScalarPoint) + verbs_.capacity() * sizeof(Verb);
}

ScalarRect ScalarPath::GetBounds() const {
  if (points_.empty()) return {};
  ScalarRect bounds;
  bounds.SetBoundsNoCheck(points_);
  // SkPathBuilder's bounds and detached paths are empty for non-finite points.
  if (!std::isfinite(bounds.left)) return {};
  return bounds;
}

} // namespace bkfont
