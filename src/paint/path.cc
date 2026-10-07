// Ported from: skia/src/core/SkPathBuilder.cpp
// Ported from: skia/src/core/SkPath.cpp (Iter, isRect, isLine)
// Ported from: skia/src/core/SkPathPriv.cpp (IsRectContour)
// Ported from: skia/src/core/SkPathRawShapes.cpp
// Ported from: skia/src/core/SkPathMakers.h

#include "path.h"

#include <cmath>
#include "path_geometry.h"

namespace bkit {

ScalarRect ScalarPath::ComputeTightBounds() const {
  if (points_.empty() || !IsFinite()) return {};
  ScalarRect bounds{points_[0].x, points_[0].y, points_[0].x, points_[0].y};
  const auto add = [&](ScalarPoint p) {
    bounds.left = std::min(bounds.left, p.x);
    bounds.top = std::min(bounds.top, p.y);
    bounds.right = std::max(bounds.right, p.x);
    bounds.bottom = std::max(bounds.bottom, p.y);
  };
  Iter iter(*this, false);
  ScalarPoint p[4];
  while (const auto verb = iter.Next(p)) {
    if (*verb == Verb::kMove) { add(p[0]); continue; }
    if (*verb == Verb::kClose) continue;
    if (*verb == Verb::kLine) { add(p[1]); continue; }
    const bool cubic = *verb == Verb::kCubic;
    add(p[cubic ? 3 : 2]);
    for (int axis = 0; axis < 2; ++axis) {
      const auto v = [&](int i) { return axis ? p[i].y : p[i].x; };
      float roots[2];
      int count = 0;
      if (cubic) {
        count = FindUnitQuadRoots(-v(0) + 3 * v(1) - 3 * v(2) + v(3),
                                  2 * (v(0) - 2 * v(1) + v(2)), v(1) - v(0), roots);
      } else if (*verb == Verb::kConic) {
        const float w = iter.ConicWeight();
        const float a = v(0) - 2 * w * v(1) + v(2), b = 2 * (w * v(1) - v(0));
        const float d = 2 * (1 - w), e = 2 * (w - 1);
        count = FindUnitQuadRoots(a * e - b * d, 2 * (a - v(0) * d), b - v(0) * e, roots);
      } else {
        const float denominator = v(0) - 2 * v(1) + v(2);
        const float t = (v(0) - v(1)) / denominator;
        if (t > 0 && t < 1) roots[count++] = t;
      }
      for (int i = 0; i < count; ++i) {
        if (cubic) {
          ScalarPoint pt;
          EvalCubicAt(p, roots[i], &pt, nullptr, nullptr);
          add(pt);
        } else if (*verb == Verb::kConic) {
          add(Conic(p, iter.ConicWeight()).EvalAt(roots[i]));
        } else {
          add(EvalQuadAt(p, roots[i]));
        }
      }
    }
  }
  return bounds;
}

namespace {

constexpr float kScalarRoot2Over2 = 0.707106781f;

// SkPath_PointIterator.
template <unsigned N>
class PointIterator {
public:
  PointIterator(PathDirection dir, unsigned start_index)
      : current_(start_index % N),
        advance_(dir == PathDirection::kCW ? 1 : N - 1) {
  }

  const ScalarPoint& Current() const {
    return pts_[current_];
  }
  const ScalarPoint& Next() {
    current_ = (current_ + advance_) % N;
    return Current();
  }

protected:
  ScalarPoint pts_[N];

private:
  unsigned current_;
  unsigned advance_;
};

// SkPath_RectPointIterator.
class RectPointIterator : public PointIterator<4> {
public:
  RectPointIterator(const ScalarRect& rect, PathDirection dir, unsigned start_index)
      : PointIterator(dir, start_index) {
    pts_[0] = {rect.left, rect.top};
    pts_[1] = {rect.right, rect.top};
    pts_[2] = {rect.right, rect.bottom};
    pts_[3] = {rect.left, rect.bottom};
  }
};

// SkPath_OvalPointIterator.
class OvalPointIterator : public PointIterator<4> {
public:
  OvalPointIterator(const ScalarRect& oval, PathDirection dir, unsigned start_index)
      : PointIterator(dir, start_index) {
    const float cx = oval.CenterX();
    const float cy = oval.CenterY();
    pts_[0] = {cx, oval.top};
    pts_[1] = {oval.right, cy};
    pts_[2] = {cx, oval.bottom};
    pts_[3] = {oval.left, cy};
  }
};

// SkPath_RRectPointIterator for equal corner radii.
class RRectPointIterator : public PointIterator<8> {
public:
  RRectPointIterator(const ScalarRect& bounds, float rx, float ry, PathDirection dir, unsigned start_index)
      : PointIterator(dir, start_index) {
    const float l = bounds.left;
    const float t = bounds.top;
    const float r = bounds.right;
    const float b = bounds.bottom;
    pts_[0] = {l + rx, t};
    pts_[1] = {r - rx, t};
    pts_[2] = {r, t + ry};
    pts_[3] = {r, b - ry};
    pts_[4] = {r - rx, b};
    pts_[5] = {l + rx, b};
    pts_[6] = {l, b - ry};
    pts_[7] = {l, t + ry};
  }
};

// rect_make_dir() of IsRectContour: 0x1 is set if the segment is horizontal,
// 0x2 if it is moving to the right or down.
int RectMakeDir(float dx, float dy) {
  return ((0 != dx) << 0) | ((dx > 0 || dy > 0) << 1);
}

} // namespace

int ScalarPath::PtsInVerb(Verb verb) {
  switch (verb) {
  case Verb::kMove:
    return 1;
  case Verb::kLine:
    return 1;
  case Verb::kQuad:
    return 2;
  case Verb::kConic:
    return 2;
  case Verb::kCubic:
    return 3;
  case Verb::kClose:
    return 0;
  }
  return 0;
}

void ScalarPath::Reset() {
  points_.clear();
  verbs_.clear();
  conic_weights_.clear();
  last_move_ = {};
  needs_move_ = true;
  fill_type_ = FillType::kWinding;
}

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

void ScalarPath::ConicTo(ScalarPoint control, ScalarPoint end, float weight) {
  EnsureMove();
  points_.push_back(control);
  points_.push_back(end);
  verbs_.push_back(Verb::kConic);
  conic_weights_.push_back(weight);
}

void ScalarPath::CubicTo(ScalarPoint control1, ScalarPoint control2, ScalarPoint end) {
  EnsureMove();
  points_.push_back(control1);
  points_.push_back(control2);
  points_.push_back(end);
  verbs_.push_back(Verb::kCubic);
}

void ScalarPath::Close() {
  // If this is a 2nd 'close', we just ignore it
  if (!verbs_.empty() && verbs_.back() != Verb::kClose) {
    EnsureMove();
    verbs_.push_back(Verb::kClose);
    // last_move_ stays where it is -- the previous moveTo
    needs_move_ = true;
  }
}

void ScalarPath::AddRect(const ScalarRect& rect, PathDirection dir, unsigned start_index) {
  RectPointIterator iter(rect, dir, start_index);
  MoveTo(iter.Current());
  LineTo(iter.Next());
  LineTo(iter.Next());
  LineTo(iter.Next());
  Close();
}

void ScalarPath::AddOval(const ScalarRect& oval, PathDirection dir, unsigned start_index) {
  OvalPointIterator oval_iter(oval, dir, start_index);
  RectPointIterator rect_iter(oval, dir, start_index + (dir == PathDirection::kCW ? 0 : 1));
  MoveTo(oval_iter.Current());
  for (unsigned i = 0; i < 4; ++i) {
    const ScalarPoint control = rect_iter.Next();
    ConicTo(control, oval_iter.Next(), kScalarRoot2Over2);
  }
  Close();
}

void ScalarPath::AddCircle(float x, float y, float radius, PathDirection dir) {
  if (radius >= 0) AddOval(ScalarRect::MakeLTRB(x - radius, y - radius, x + radius, y + radius), dir);
}

void ScalarPath::AddRRect(const ScalarRect& rect, float x_radius, float y_radius, PathDirection dir,
                          unsigned start_index) {
  // SkRRect::setRectXY.
  ScalarRect bounds = rect;
  bounds.Sort();
  const bool is_empty = !bounds.IsFinite() || bounds.IsEmpty();
  if (!std::isfinite(x_radius) || !std::isfinite(y_radius)) x_radius = y_radius = 0;
  if (bounds.Width() < x_radius + x_radius || bounds.Height() < y_radius + y_radius) {
    const float scale = std::min(bounds.Width() / (x_radius + x_radius), bounds.Height() / (y_radius + y_radius));
    x_radius *= scale;
    y_radius *= scale;
  }
  // SkPathBuilder::addRRect: degenerate(rect) => radii points are
  // collapsing; degenerate(oval) => line points are collapsing.
  if (is_empty || x_radius <= 0 || y_radius <= 0) {
    AddRect(bounds, dir, (start_index + 1) / 2);
    return;
  }
  if (x_radius >= bounds.Width() * 0.5f && y_radius >= bounds.Height() * 0.5f) {
    AddOval(bounds, dir, start_index / 2);
    return;
  }
  // set_as_rrect(): we start with a conic on odd indices when moving CW vs.
  // even indices when moving CCW.
  const bool starts_with_conic = ((start_index & 1) == (dir == PathDirection::kCW));
  RRectPointIterator rrect_iter(bounds, x_radius, y_radius, dir, start_index);
  // Corner iterator indices follow the collapsed radii model, adjusted such
  // that the start pt is "behind" the radii start pt.
  const unsigned rect_start_index = start_index / 2 + (dir == PathDirection::kCW ? 0 : 1);
  RectPointIterator rect_iter(bounds, dir, rect_start_index);
  MoveTo(rrect_iter.Current());
  if (starts_with_conic) {
    for (unsigned i = 0; i < 3; ++i) {
      const ScalarPoint control = rect_iter.Next();
      ConicTo(control, rrect_iter.Next(), kScalarRoot2Over2);
      LineTo(rrect_iter.Next());
    }
    const ScalarPoint control = rect_iter.Next();
    ConicTo(control, rrect_iter.Next(), kScalarRoot2Over2);
    // the final line is accomplished by close()
  } else {
    for (unsigned i = 0; i < 4; ++i) {
      LineTo(rrect_iter.Next());
      const ScalarPoint control = rect_iter.Next();
      ConicTo(control, rrect_iter.Next(), kScalarRoot2Over2);
    }
  }
  Close();
}

void ScalarPath::AddPolygon(std::span<const ScalarPoint> points, bool is_closed) {
  if (points.empty()) return;
  MoveTo(points[0]);
  for (std::size_t i = 1; i < points.size(); ++i) LineTo(points[i]);
  if (is_closed) Close();
}

void ScalarPath::AddPath(const ScalarPath& src) {
  if (src.IsEmpty()) return;
  if (IsEmpty()) {
    const FillType fill_type = fill_type_;
    *this = src;
    fill_type_ = fill_type;
    return;
  }
  points_.insert(points_.end(), src.points_.begin(), src.points_.end());
  verbs_.insert(verbs_.end(), src.verbs_.begin(), src.verbs_.end());
  conic_weights_.insert(conic_weights_.end(), src.conic_weights_.begin(), src.conic_weights_.end());
  needs_move_ = src.needs_move_;
  last_move_ = src.last_move_;
}

void ScalarPath::ReversePathTo(const ScalarPath& path) {
  if (path.verbs_.empty()) return;
  std::size_t verb = path.verbs_.size();
  std::size_t point = path.points_.size() - 1;
  std::size_t weight = path.conic_weights_.size();
  const ScalarPoint* pts = path.points_.data();
  while (verb > 0) {
    const Verb v = path.verbs_[--verb];
    point -= static_cast<std::size_t>(PtsInVerb(v));
    switch (v) {
    case Verb::kMove:
      // if the path has multiple contours, stop after reversing the last
      return;
    case Verb::kLine:
      LineTo(pts[point]);
      break;
    case Verb::kQuad:
      QuadTo(pts[point + 1], pts[point]);
      break;
    case Verb::kConic:
      ConicTo(pts[point + 1], pts[point], path.conic_weights_[--weight]);
      break;
    case Verb::kCubic:
      CubicTo(pts[point + 2], pts[point + 1], pts[point]);
      break;
    case Verb::kClose:
      break;
    }
  }
}

std::optional<ScalarPoint> ScalarPath::GetLastPt() const {
  if (points_.empty()) return std::nullopt;
  return points_.back();
}

void ScalarPath::SetLastPt(ScalarPoint point) {
  if (points_.empty()) MoveTo(point);
  else points_.back() = point;
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
  path.AddPolygon(points, is_closed);
  return path;
}

ScalarPath ScalarPath::MakeOffset(float dx, float dy) const {
  ScalarPath result = *this;
  result.Transform(ScalarMatrix::Translate(dx, dy));
  return result;
}

std::size_t ScalarPath::ApproximateBytesUsed() const {
  return sizeof(ScalarPath) + points_.capacity() * sizeof(ScalarPoint) + verbs_.capacity() * sizeof(Verb) +
         conic_weights_.capacity() * sizeof(float);
}

ScalarRect ScalarPath::GetBounds() const {
  if (points_.empty()) return {};
  ScalarRect bounds;
  bounds.SetBoundsNoCheck(points_);
  // SkPathBuilder's bounds and detached paths are empty for non-finite points.
  if (!std::isfinite(bounds.left)) return {};
  return bounds;
}

ScalarRect ScalarPath::ComputeBounds() const {
  if (points_.empty()) return {};
  ScalarRect bounds;
  bounds.SetBoundsNoCheck(points_);
  return bounds;
}

bool ScalarPath::IsFinite() const {
  float accum = 0;
  for (const ScalarPoint& point : points_) {
    accum *= point.x;
    accum *= point.y;
  }
  return accum == 0;
}

bool ScalarPath::IsLine(ScalarPoint line[2]) const {
  if (verbs_.size() == 2 && verbs_[1] == Verb::kLine) {
    if (line) {
      line[0] = points_[0];
      line[1] = points_[1];
    }
    return true;
  }
  return false;
}

// SkPathPriv::IsRectContour() with allowPartial false, for the path's first
// contour.
bool ScalarPath::IsRect(ScalarRect* rect, bool* is_closed, PathDirection* direction) const {
  if (points_.size() < 4) return false;

  std::size_t curr_verb = 0;
  const std::size_t verb_count = verbs_.size();
  int corners = 0;
  ScalarPoint close_xy;  // used to determine if final line falls on a diagonal
  ScalarPoint line_start; // used to construct line from previous point
  const ScalarPoint* first_pt = nullptr; // first point in the rect (last of first moves)
  const ScalarPoint* last_pt = nullptr;  // last point in the rect (last of lines or first if closed)
  ScalarPoint first_corner;
  ScalarPoint third_corner;
  const ScalarPoint* pts = points_.data();
  signed char directions[] = {-1, -1, -1, -1, -1}; // -1 to 3; -1 is uninitialized
  bool closed_or_moved = false;
  bool auto_close = false;
  while (curr_verb < verb_count) {
    const Verb verb = verbs_[curr_verb];
    switch (verb) {
    case Verb::kClose:
      auto_close = true;
      [[fallthrough]];
    case Verb::kLine: {
      if (Verb::kClose != verb) last_pt = pts;
      const ScalarPoint line_end = Verb::kClose == verb ? *first_pt : *pts++;
      const ScalarPoint line_delta = line_end - line_start;
      if (line_delta.x && line_delta.y) return false; // diagonal
      if (!line_delta.IsFinite()) return false;       // path contains infinity or NaN
      if (line_start == line_end) break;             // single point on side OK
      const int next_direction = RectMakeDir(line_delta.x, line_delta.y); // 0 to 3
      if (0 == corners) {
        directions[0] = static_cast<signed char>(next_direction);
        corners = 1;
        closed_or_moved = false;
        line_start = line_end;
        break;
      }
      if (closed_or_moved) return false; // closed followed by a line
      if (auto_close && next_direction == directions[0]) break; // colinear with first
      closed_or_moved = auto_close;
      if (directions[corners - 1] == next_direction) {
        if (3 == corners && Verb::kLine == verb) third_corner = line_end;
        line_start = line_end;
        break; // colinear segment
      }
      directions[corners++] = static_cast<signed char>(next_direction);
      // opposite lines must point in opposite directions; xoring them should equal 2
      switch (corners) {
      case 2:
        first_corner = line_start;
        break;
      case 3:
        if ((directions[0] ^ directions[2]) != 2) return false;
        third_corner = line_end;
        break;
      case 4:
        if ((directions[1] ^ directions[3]) != 2) return false;
        break;
      default:
        return false; // too many direction changes
      }
      line_start = line_end;
      break;
    }
    case Verb::kQuad:
    case Verb::kConic:
    case Verb::kCubic:
      return false; // quadratic, cubic not allowed
    case Verb::kMove:
      if (!corners) {
        first_pt = pts;
      } else {
        close_xy = *first_pt - *last_pt;
        if (close_xy.x && close_xy.y) return false; // we're diagonal, abort
      }
      line_start = *pts++;
      closed_or_moved = true;
      break;
    }
    curr_verb += 1;
  }
  // Success if 4 corners and first point equals last
  if (corners < 3 || corners > 4) return false;
  // check if close generates diagonal
  close_xy = *first_pt - *last_pt;
  if (close_xy.x && close_xy.y) return false;
  if (rect) *rect = ScalarRect::MakeBounds(first_corner, third_corner);
  if (is_closed) *is_closed = auto_close;
  if (direction) {
    *direction = directions[0] == ((directions[1] + 1) & 3) ? PathDirection::kCW : PathDirection::kCCW;
  }
  return true;
}

ScalarPath::Iter::Iter(const ScalarPath& path, bool force_close)
    : path_(&path),
      force_close_(force_close) {
}

ScalarPath::Verb ScalarPath::Iter::AutoClose(ScalarPoint pts[2]) {
  if (!(last_pt_ == move_to_)) {
    // A special case: if both points are NaN, SkPoint::operation== returns
    // false, but the iterator expects that they are treated as the same.
    if (std::isnan(last_pt_.x) || std::isnan(last_pt_.y) || std::isnan(move_to_.x) || std::isnan(move_to_.y))
      return Verb::kClose;
    pts[0] = last_pt_;
    pts[1] = move_to_;
    last_pt_ = move_to_;
    close_line_ = true;
    return Verb::kLine;
  }
  pts[0] = move_to_;
  return Verb::kClose;
}

std::optional<ScalarPath::Verb> ScalarPath::Iter::Next(ScalarPoint pts[4]) {
  const auto& verbs = path_->verbs_;
  const auto& points = path_->points_;
  if (verb_index_ == verbs.size()) {
    // Close the curve if requested and if there is some curve to close
    if (need_close_) {
      if (Verb::kLine == AutoClose(pts)) return Verb::kLine;
      need_close_ = false;
      return Verb::kClose;
    }
    return std::nullopt;
  }

  Verb verb = verbs[verb_index_++];
  switch (verb) {
  case Verb::kMove:
    if (need_close_) {
      verb_index_--; // move back one verb
      verb = AutoClose(pts);
      if (verb == Verb::kClose) need_close_ = false;
      return verb;
    }
    if (verb_index_ == verbs.size()) return std::nullopt; // might be a trailing moveto
    move_to_ = points[point_index_];
    pts[0] = move_to_;
    point_index_ += 1;
    last_pt_ = move_to_;
    need_close_ = force_close_;
    break;
  case Verb::kLine:
    pts[0] = last_pt_;
    pts[1] = points[point_index_];
    last_pt_ = pts[1];
    close_line_ = false;
    point_index_ += 1;
    break;
  case Verb::kConic:
    conic_weight_ = path_->conic_weights_[weight_index_++];
    [[fallthrough]];
  case Verb::kQuad:
    pts[0] = last_pt_;
    pts[1] = points[point_index_];
    pts[2] = points[point_index_ + 1];
    last_pt_ = pts[2];
    point_index_ += 2;
    break;
  case Verb::kCubic:
    pts[0] = last_pt_;
    pts[1] = points[point_index_];
    pts[2] = points[point_index_ + 1];
    pts[3] = points[point_index_ + 2];
    last_pt_ = pts[3];
    point_index_ += 3;
    break;
  case Verb::kClose:
    verb = AutoClose(pts);
    if (verb == Verb::kLine) verb_index_--; // move back one verb
    else need_close_ = false;
    last_pt_ = move_to_;
    break;
  }
  return verb;
}

} // namespace bkit
