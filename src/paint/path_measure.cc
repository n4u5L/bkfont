// Ported from: skia/src/core/SkContourMeasure.cpp
// Ported from: skia/src/core/SkPathMeasure.cpp

#include "path_measure.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "path_geometry.h"

namespace bkfont {

namespace {

// SkSegType.
enum SegType {
  kLine_SegType,
  kQuad_SegType,
  kCubic_SegType,
  kConic_SegType,
};

constexpr int kMaxTValue = 0x3FFFFFFF;

constexpr float TValue2Scalar(int t) {
  const float kMaxTReciprocal = 1.0f / static_cast<float>(kMaxTValue);
  return static_cast<float>(t) * kMaxTReciprocal;
}

float ScalarInterp(float a, float b, float t) {
  return a + (b - a) * t;
}

// SkContourMeasure_segTo.
void SegTo(const ScalarPoint pts[], unsigned seg_type, float start_t, float stop_t, ScalarPath* dst) {
  if (start_t == stop_t) {
    if (!dst->IsEmpty()) {
      // if the dash as a zero-length on segment, add a corresponding
      // zero-length line. The stroke code will add end caps to zero length
      // lines as appropriate
      dst->LineTo(*dst->GetLastPt());
    }
    return;
  }

  ScalarPoint tmp0[7], tmp1[7];
  switch (seg_type) {
  case kLine_SegType:
    if (1 == stop_t) {
      dst->LineTo(pts[1]);
    } else {
      dst->LineTo({ScalarInterp(pts[0].x, pts[1].x, stop_t), ScalarInterp(pts[0].y, pts[1].y, stop_t)});
    }
    break;
  case kQuad_SegType:
    if (0 == start_t) {
      if (1 == stop_t) {
        dst->QuadTo(pts[1], pts[2]);
      } else {
        ChopQuadAt(pts, tmp0, stop_t);
        dst->QuadTo(tmp0[1], tmp0[2]);
      }
    } else {
      ChopQuadAt(pts, tmp0, start_t);
      if (1 == stop_t) {
        dst->QuadTo(tmp0[3], tmp0[4]);
      } else {
        ChopQuadAt(&tmp0[2], tmp1, (stop_t - start_t) / (1 - start_t));
        dst->QuadTo(tmp1[1], tmp1[2]);
      }
    }
    break;
  case kConic_SegType: {
    const Conic conic(pts[0], pts[2], pts[3], pts[1].x);
    if (0 == start_t) {
      if (1 == stop_t) {
        dst->ConicTo(conic.pts[1], conic.pts[2], conic.w);
      } else {
        Conic tmp[2];
        if (conic.ChopAt(stop_t, tmp)) dst->ConicTo(tmp[0].pts[1], tmp[0].pts[2], tmp[0].w);
      }
    } else {
      if (1 == stop_t) {
        Conic tmp[2];
        if (conic.ChopAt(start_t, tmp)) dst->ConicTo(tmp[1].pts[1], tmp[1].pts[2], tmp[1].w);
      } else {
        Conic tmp;
        conic.ChopAt(start_t, stop_t, &tmp);
        dst->ConicTo(tmp.pts[1], tmp.pts[2], tmp.w);
      }
    }
  } break;
  case kCubic_SegType:
    if (0 == start_t) {
      if (1 == stop_t) {
        dst->CubicTo(pts[1], pts[2], pts[3]);
      } else {
        ChopCubicAt(pts, tmp0, stop_t);
        dst->CubicTo(tmp0[1], tmp0[2], tmp0[3]);
      }
    } else {
      ChopCubicAt(pts, tmp0, start_t);
      if (1 == stop_t) {
        dst->CubicTo(tmp0[4], tmp0[5], tmp0[6]);
      } else {
        ChopCubicAt(&tmp0[3], tmp1, (stop_t - start_t) / (1 - start_t));
        dst->CubicTo(tmp1[1], tmp1[2], tmp1[3]);
      }
    }
    break;
  }
}

int TspanBigEnough(int tspan) {
  return tspan >> 10;
}

constexpr float kCheapDistLimit = 0.5f; // just made this value up

bool QuadTooCurvy(const ScalarPoint pts[3], float tolerance) {
  // diff = (a/4 + b/2 + c/4) - (a/2 + c/2)
  // diff = -a/4 + b/2 - c/4
  const float dx = pts[1].x * 0.5f - (pts[0].x + pts[2].x) * 0.5f * 0.5f;
  const float dy = pts[1].y * 0.5f - (pts[0].y + pts[2].y) * 0.5f * 0.5f;
  const float dist = std::max(std::fabs(dx), std::fabs(dy));
  return dist > tolerance;
}

bool ConicTooCurvy(ScalarPoint first_pt, ScalarPoint mid_t_pt, ScalarPoint last_pt, float tolerance) {
  ScalarPoint mid_ends = first_pt + last_pt;
  mid_ends *= 0.5f;
  const ScalarPoint dxy = mid_t_pt - mid_ends;
  const float dist = std::max(std::fabs(dxy.x), std::fabs(dxy.y));
  return dist > tolerance;
}

bool CheapDistExceedsLimit(ScalarPoint pt, float x, float y, float tolerance) {
  const float dist = std::max(std::fabs(x - pt.x), std::fabs(y - pt.y));
  // just made up the 1/2
  return dist > tolerance;
}

bool CubicTooCurvy(const ScalarPoint pts[4], float tolerance) {
  return CheapDistExceedsLimit(pts[1], ScalarInterp(pts[0].x, pts[3].x, 1.0f / 3),
                               ScalarInterp(pts[0].y, pts[3].y, 1.0f / 3), tolerance) ||
         CheapDistExceedsLimit(pts[2], ScalarInterp(pts[0].x, pts[3].x, 1.0f * 2 / 3),
                               ScalarInterp(pts[0].y, pts[3].y, 1.0f * 2 / 3), tolerance);
}

constexpr int kMaxRecursionDepth = 8;

void ComputePosTan(const ScalarPoint pts[], unsigned seg_type, float t, ScalarPoint* pos, ScalarPoint* tangent) {
  switch (seg_type) {
  case kLine_SegType:
    if (pos) *pos = {ScalarInterp(pts[0].x, pts[1].x, t), ScalarInterp(pts[0].y, pts[1].y, t)};
    if (tangent) point::SetNormalize(tangent, pts[1].x - pts[0].x, pts[1].y - pts[0].y);
    break;
  case kQuad_SegType:
    EvalQuadAt(pts, t, pos, tangent);
    if (tangent) point::Normalize(tangent);
    break;
  case kConic_SegType: {
    Conic(pts[0], pts[2], pts[3], pts[1].x).EvalAt(t, pos, tangent);
    if (tangent) point::Normalize(tangent);
  } break;
  case kCubic_SegType:
    EvalCubicAt(pts, t, pos, tangent, nullptr);
    if (tangent) point::Normalize(tangent);
    break;
  }
}

// SkTKSearch.
int TKSearch(const ContourMeasure::Segment base[], int count, float key) {
  if (count <= 0) return ~0;
  unsigned lo = 0;
  unsigned hi = static_cast<unsigned>(count - 1);
  while (lo < hi) {
    const unsigned mid = (hi + lo) >> 1;
    if (base[mid].distance < key) lo = mid + 1;
    else hi = mid;
  }
  if (base[hi].distance < key) {
    hi += 1;
    hi = ~hi;
  } else if (key < base[hi].distance) {
    hi = ~hi;
  }
  return static_cast<int>(hi);
}

} // namespace

float ContourMeasure::Segment::GetScalarT() const {
  return TValue2Scalar(static_cast<int>(t_value));
}

// SkContourMeasureIter::Impl.
class ContourMeasureIter::Impl {
public:
  Impl(const ScalarPath& path, bool force_closed, float res_scale)
      : path_(path),
        tolerance_(kCheapDistLimit * (1.0f / res_scale)),
        force_closed_(force_closed) {
  }

  bool HasNextSegments() const {
    return verb_index_ < path_.Verbs().size();
  }
  std::unique_ptr<ContourMeasure> BuildSegments();

private:
  float ComputeLineSeg(ScalarPoint p0, ScalarPoint p1, float distance, unsigned pt_index);
  float ComputeQuadSegs(const ScalarPoint pts[3], float distance, int mint, int maxt, unsigned pt_index,
                        int recursion_depth = 0);
  float ComputeConicSegs(const Conic& conic, float distance, int mint, ScalarPoint min_pt, int maxt,
                         ScalarPoint max_pt, unsigned pt_index, int recursion_depth = 0);
  float ComputeCubicSegs(const ScalarPoint pts[4], float distance, int mint, int maxt, unsigned pt_index,
                         int recursion_depth = 0);
  void AppendSegment(float distance, unsigned pt_index, SegType type, int t_value) {
    ContourMeasure::Segment seg;
    seg.distance = distance;
    seg.pt_index = pt_index;
    seg.type = type;
    seg.t_value = static_cast<unsigned>(t_value);
    segments_.push_back(seg);
  }

  ScalarPath path_;
  // SkPathPriv::RangeIter state.
  std::size_t verb_index_ = 0;
  std::size_t point_index_ = 0;
  std::size_t weight_index_ = 0;
  ScalarPoint last_pt_;
  float tolerance_;
  bool force_closed_;

  std::vector<ContourMeasure::Segment> segments_;
  std::vector<ScalarPoint> pts_; // Points used to define the segments
};

float ContourMeasureIter::Impl::ComputeQuadSegs(const ScalarPoint pts[3], float distance, int mint, int maxt,
                                                unsigned pt_index, int recursion_depth) {
  if (recursion_depth < kMaxRecursionDepth && TspanBigEnough(maxt - mint) && QuadTooCurvy(pts, tolerance_)) {
    ScalarPoint tmp[5];
    const int halft = (mint + maxt) >> 1;
    ChopQuadAtHalf(pts, tmp);
    recursion_depth += 1;
    distance = ComputeQuadSegs(tmp, distance, mint, halft, pt_index, recursion_depth);
    distance = ComputeQuadSegs(&tmp[2], distance, halft, maxt, pt_index, recursion_depth);
  } else {
    const float d = point::Distance(pts[0], pts[2]);
    const float prev_d = distance;
    distance += d;
    if (distance > prev_d) AppendSegment(distance, pt_index, kQuad_SegType, maxt);
  }
  return distance;
}

float ContourMeasureIter::Impl::ComputeConicSegs(const Conic& conic, float distance, int mint, ScalarPoint min_pt,
                                                 int maxt, ScalarPoint max_pt, unsigned pt_index,
                                                 int recursion_depth) {
  const int halft = (mint + maxt) >> 1;
  const ScalarPoint half_pt = conic.EvalAt(TValue2Scalar(halft));
  if (!half_pt.IsFinite()) return distance;
  if (recursion_depth < kMaxRecursionDepth && TspanBigEnough(maxt - mint) &&
      ConicTooCurvy(min_pt, half_pt, max_pt, tolerance_)) {
    recursion_depth += 1;
    distance = ComputeConicSegs(conic, distance, mint, min_pt, halft, half_pt, pt_index, recursion_depth);
    distance = ComputeConicSegs(conic, distance, halft, half_pt, maxt, max_pt, pt_index, recursion_depth);
  } else {
    const float d = point::Distance(min_pt, max_pt);
    const float prev_d = distance;
    distance += d;
    if (distance > prev_d) AppendSegment(distance, pt_index, kConic_SegType, maxt);
  }
  return distance;
}

float ContourMeasureIter::Impl::ComputeCubicSegs(const ScalarPoint pts[4], float distance, int mint, int maxt,
                                                 unsigned pt_index, int recursion_depth) {
  if (recursion_depth < kMaxRecursionDepth && TspanBigEnough(maxt - mint) && CubicTooCurvy(pts, tolerance_)) {
    ScalarPoint tmp[7];
    const int halft = (mint + maxt) >> 1;
    ChopCubicAtHalf(pts, tmp);
    recursion_depth += 1;
    distance = ComputeCubicSegs(tmp, distance, mint, halft, pt_index, recursion_depth);
    distance = ComputeCubicSegs(&tmp[3], distance, halft, maxt, pt_index, recursion_depth);
  } else {
    const float d = point::Distance(pts[0], pts[3]);
    const float prev_d = distance;
    distance += d;
    if (distance > prev_d) AppendSegment(distance, pt_index, kCubic_SegType, maxt);
  }
  return distance;
}

float ContourMeasureIter::Impl::ComputeLineSeg(ScalarPoint p0, ScalarPoint p1, float distance, unsigned pt_index) {
  const float d = point::Distance(p0, p1);
  const float prev_d = distance;
  distance += d;
  if (distance > prev_d) AppendSegment(distance, pt_index, kLine_SegType, kMaxTValue);
  return distance;
}

std::unique_ptr<ContourMeasure> ContourMeasureIter::Impl::BuildSegments() {
  int pt_index = -1;
  float distance = 0;
  bool have_seen_close = force_closed_;
  bool have_seen_move_to = false;

  // Note: as we accumulate distance, we have to check that the result of +=
  // actually made it larger, since a very small delta might be > 0, but still
  // have no effect on distance (if distance >>> delta).
  //
  // We do this check below, and in compute_quad_segs and compute_cubic_segs
  segments_.clear();
  pts_.clear();

  const auto verbs = path_.Verbs();
  const auto points = path_.Points();
  const auto weights = path_.ConicWeights();
  for (; verb_index_ < verbs.size(); ++verb_index_) {
    const ScalarPath::Verb verb = verbs[verb_index_];
    if (have_seen_move_to && verb == ScalarPath::Verb::kMove) break;
    // SkPathPriv::RangeIter: the points of the segment, starting at the
    // previous point.
    ScalarPoint pts[4];
    pts[0] = last_pt_;
    const int count = ScalarPath::PtsInVerb(verb);
    for (int i = 0; i < count; ++i) pts[verb == ScalarPath::Verb::kMove ? 0 : i + 1] = points[point_index_ + i];
    switch (verb) {
    case ScalarPath::Verb::kMove:
      pt_index += 1;
      pts_.push_back(pts[0]);
      have_seen_move_to = true;
      break;
    case ScalarPath::Verb::kLine: {
      const float prev_d = distance;
      distance = ComputeLineSeg(pts[0], pts[1], distance, static_cast<unsigned>(pt_index));
      if (distance > prev_d) {
        pts_.push_back(pts[1]);
        pt_index++;
      }
    } break;
    case ScalarPath::Verb::kQuad: {
      const float prev_d = distance;
      distance = ComputeQuadSegs(pts, distance, 0, kMaxTValue, static_cast<unsigned>(pt_index));
      if (distance > prev_d) {
        pts_.push_back(pts[1]);
        pts_.push_back(pts[2]);
        pt_index += 2;
      }
    } break;
    case ScalarPath::Verb::kConic: {
      const Conic conic(pts, weights[weight_index_]);
      const float prev_d = distance;
      distance = ComputeConicSegs(conic, distance, 0, conic.pts[0], kMaxTValue, conic.pts[2],
                                  static_cast<unsigned>(pt_index));
      if (distance > prev_d) {
        pts_.push_back({conic.w, 0});
        pts_.push_back(pts[1]);
        pts_.push_back(pts[2]);
        pt_index += 3;
      }
    } break;
    case ScalarPath::Verb::kCubic: {
      const float prev_d = distance;
      distance = ComputeCubicSegs(pts, distance, 0, kMaxTValue, static_cast<unsigned>(pt_index));
      if (distance > prev_d) {
        pts_.push_back(pts[1]);
        pts_.push_back(pts[2]);
        pts_.push_back(pts[3]);
        pt_index += 3;
      }
    } break;
    case ScalarPath::Verb::kClose:
      have_seen_close = true;
      break;
    }
    if (verb == ScalarPath::Verb::kConic) ++weight_index_;
    if (count) last_pt_ = points[point_index_ + count - 1];
    point_index_ += static_cast<std::size_t>(count);
  }

  if (!std::isfinite(distance)) return nullptr;
  if (segments_.empty()) return nullptr;

  if (have_seen_close) {
    const float prev_d = distance;
    const ScalarPoint first_pt = pts_[0];
    distance = ComputeLineSeg(pts_[static_cast<std::size_t>(pt_index)], first_pt, distance,
                              static_cast<unsigned>(pt_index));
    if (distance > prev_d) pts_.push_back(first_pt);
  }
  return std::make_unique<ContourMeasure>(std::move(segments_), std::move(pts_), distance, have_seen_close);
}

ContourMeasureIter::ContourMeasureIter(const ScalarPath& path, bool force_closed, float res_scale) {
  if (path.IsFinite()) impl_ = std::make_unique<Impl>(path, force_closed, res_scale);
}

ContourMeasureIter::~ContourMeasureIter() = default;

std::unique_ptr<ContourMeasure> ContourMeasureIter::Next() {
  if (!impl_) return nullptr;
  while (impl_->HasNextSegments()) {
    if (auto cm = impl_->BuildSegments()) return cm;
  }
  return nullptr;
}

ContourMeasure::ContourMeasure(std::vector<Segment> segments, std::vector<ScalarPoint> pts, float length,
                               bool is_closed)
    : segments_(std::move(segments)),
      pts_(std::move(pts)),
      length_(length),
      is_closed_(is_closed) {
}

const ContourMeasure::Segment* ContourMeasure::DistanceToSegment(float distance, float* t) const {
  const Segment* seg = segments_.data();
  const int count = static_cast<int>(segments_.size());
  int index = TKSearch(seg, count, distance);
  // don't care if we hit an exact match or not, so we xor index if it is negative
  index ^= (index >> 31);
  seg = &seg[index];

  // now interpolate t-values with the prev segment (if possible)
  float start_t = 0, start_d = 0;
  // check if the prev segment is legal, and references the same set of points
  if (index > 0) {
    start_d = seg[-1].distance;
    if (seg[-1].pt_index == seg->pt_index) start_t = seg[-1].GetScalarT();
  }
  *t = start_t + (seg->GetScalarT() - start_t) * (distance - start_d) / (seg->distance - start_d);
  return seg;
}

bool ContourMeasure::GetPosTan(float distance, ScalarPoint* pos, ScalarPoint* tangent) const {
  if (std::isnan(distance)) return false;
  const float length = Length();
  // pin the distance to a legal range
  if (distance < 0) distance = 0;
  else if (distance > length) distance = length;
  float t;
  const Segment* seg = DistanceToSegment(distance, &t);
  if (std::isnan(t)) return false;
  ComputePosTan(&pts_[seg->pt_index], seg->type, t, pos, tangent);
  return true;
}

bool ContourMeasure::GetSegment(float start_d, float stop_d, ScalarPath* dst, bool start_with_move_to) const {
  const float length = Length(); // ensure we have built our segments
  if (start_d < 0) start_d = 0;
  if (stop_d > length) stop_d = length;
  if (!(start_d <= stop_d)) return false; // catch NaN values as well
  if (segments_.empty()) return false;

  ScalarPoint p;
  float start_t, stop_t;
  const Segment* seg = DistanceToSegment(start_d, &start_t);
  if (!std::isfinite(start_t)) return false;
  const Segment* stop_seg = DistanceToSegment(stop_d, &stop_t);
  if (!std::isfinite(stop_t)) return false;
  if (start_with_move_to) {
    ComputePosTan(&pts_[seg->pt_index], seg->type, start_t, &p, nullptr);
    dst->MoveTo(p);
  }

  if (seg->pt_index == stop_seg->pt_index) {
    SegTo(&pts_[seg->pt_index], seg->type, start_t, stop_t, dst);
  } else {
    do {
      SegTo(&pts_[seg->pt_index], seg->type, start_t, 1, dst);
      // SkContourMeasure::Segment::Next: the first segment with a different
      // pt_index.
      const unsigned pt_index = seg->pt_index;
      do {
        ++seg;
      } while (seg->pt_index == pt_index);
      start_t = 0;
    } while (seg->pt_index < stop_seg->pt_index);
    SegTo(&pts_[seg->pt_index], seg->type, 0, stop_t, dst);
  }
  return true;
}

PathMeasure::PathMeasure(const ScalarPath& path, bool force_closed, float res_scale)
    : iter_(path, force_closed, res_scale) {
  contour_ = iter_.Next();
}

PathMeasure::~PathMeasure() = default;

float PathMeasure::GetLength() const {
  return contour_ ? contour_->Length() : 0;
}

bool PathMeasure::GetSegment(float start_d, float stop_d, ScalarPath* dst, bool start_with_move_to) const {
  return contour_ && contour_->GetSegment(start_d, stop_d, dst, start_with_move_to);
}

bool PathMeasure::IsClosed() const {
  return contour_ && contour_->IsClosed();
}

bool PathMeasure::NextContour() {
  contour_ = iter_.Next();
  return static_cast<bool>(contour_);
}

} // namespace bkfont
