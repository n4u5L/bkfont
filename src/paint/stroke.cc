// Ported from: skia/src/core/SkStrokeRec.cpp
// Ported from: skia/src/core/SkStroke.cpp
// Ported from: skia/src/core/SkStrokerPriv.cpp

#include "stroke.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

#include "path_geometry.h"
#include "platform_paint.h"

namespace bkfont {

namespace {

constexpr float kScalarRoot2Over2 = 0.707106781f;
constexpr float kScalarSqrt2 = 1.41421356f;

// SkStrokerPriv::CapProc / JoinProc.
using CapProc = void (*)(ScalarPath* path, ScalarPoint pivot, ScalarPoint normal, ScalarPoint stop,
                         bool extend_last_pt);
using JoinProc = void (*)(ScalarPath* outer, ScalarPath* inner, ScalarPoint before_unit_normal, ScalarPoint pivot,
                          ScalarPoint after_unit_normal, float radius, float inv_miter_limit, bool prev_is_line,
                          bool curr_is_line);

void ButtCapper(ScalarPath* sink, ScalarPoint, ScalarPoint, ScalarPoint stop, bool) {
  sink->LineTo(stop);
}

void RoundCapper(ScalarPath* sink, ScalarPoint pivot, ScalarPoint normal, ScalarPoint stop, bool) {
  ScalarPoint parallel;
  point::RotateCW(normal, &parallel);
  const ScalarPoint projected_center = pivot + parallel;
  sink->ConicTo(projected_center + normal, projected_center, kScalarRoot2Over2);
  sink->ConicTo(projected_center - normal, stop, kScalarRoot2Over2);
}

void SquareCapper(ScalarPath* sink, ScalarPoint pivot, ScalarPoint normal, ScalarPoint stop, bool extend_last_pt) {
  ScalarPoint parallel;
  point::RotateCW(normal, &parallel);
  if (extend_last_pt) {
    sink->SetLastPt({pivot.x + normal.x + parallel.x, pivot.y + normal.y + parallel.y});
    sink->LineTo({pivot.x - normal.x + parallel.x, pivot.y - normal.y + parallel.y});
  } else {
    sink->LineTo({pivot.x + normal.x + parallel.x, pivot.y + normal.y + parallel.y});
    sink->LineTo({pivot.x - normal.x + parallel.x, pivot.y - normal.y + parallel.y});
    sink->LineTo(stop);
  }
}

bool IsClockwise(ScalarPoint before, ScalarPoint after) {
  return before.x * after.y > before.y * after.x;
}

enum AngleType {
  kNearly180_AngleType,
  kSharp_AngleType,
  kShallow_AngleType,
  kNearlyLine_AngleType
};

AngleType Dot2AngleType(float dot) {
  if (dot >= 0) { // shallow or line
    return point::ScalarNearlyZero(1 - dot) ? kNearlyLine_AngleType : kShallow_AngleType;
  }
  // sharp or 180
  return point::ScalarNearlyZero(1 + dot) ? kNearly180_AngleType : kSharp_AngleType;
}

void HandleInnerJoin(ScalarPath* inner, ScalarPoint pivot, ScalarPoint after) {
  // In the degenerate case that the stroke radius is larger than our segments
  // just connecting the two inner segments may "show through" as a funny
  // diagonal. To pseudo-fix this, we go through the pivot point. This adds an
  // extra point/edge, but I can't see a cheap way to know when this is not
  // needed :(
  inner->LineTo(pivot);
  inner->LineTo({pivot.x - after.x, pivot.y - after.y});
}

void BluntJoiner(ScalarPath* outer, ScalarPath* inner, ScalarPoint before_unit_normal, ScalarPoint pivot,
                 ScalarPoint after_unit_normal, float radius, float, bool, bool) {
  ScalarPoint after = after_unit_normal * radius;
  if (!IsClockwise(before_unit_normal, after_unit_normal)) {
    std::swap(outer, inner);
    after = -after;
  }
  outer->LineTo({pivot.x + after.x, pivot.y + after.y});
  HandleInnerJoin(inner, pivot, after);
}

void RoundJoiner(ScalarPath* outer, ScalarPath* inner, ScalarPoint before_unit_normal, ScalarPoint pivot,
                 ScalarPoint after_unit_normal, float radius, float, bool, bool) {
  const float dot_prod = before_unit_normal.Dot(after_unit_normal);
  const AngleType angle_type = Dot2AngleType(dot_prod);
  if (angle_type == kNearlyLine_AngleType) return;

  ScalarPoint before = before_unit_normal;
  ScalarPoint after = after_unit_normal;
  RotationDirection dir = RotationDirection::kCW;
  if (!IsClockwise(before, after)) {
    std::swap(outer, inner);
    before = -before;
    after = -after;
    dir = RotationDirection::kCCW;
  }

  ScalarMatrix matrix;
  matrix.SetScale(radius, radius);
  matrix.PostTranslate(pivot.x, pivot.y);
  Conic conics[Conic::kMaxConicsForArc];
  const int count = Conic::BuildUnitArc(before, after, dir, &matrix, conics);
  if (count > 0) {
    for (int i = 0; i < count; ++i) outer->ConicTo(conics[i].pts[1], conics[i].pts[2], conics[i].w);
    after *= radius;
    HandleInnerJoin(inner, pivot, after);
  }
}

constexpr float kOneOverSqrt2 = 0.707106781f;

void MiterJoiner(ScalarPath* outer, ScalarPath* inner, ScalarPoint before_unit_normal, ScalarPoint pivot,
                 ScalarPoint after_unit_normal, float radius, float inv_miter_limit, bool prev_is_line,
                 bool curr_is_line) {
  // negate the dot since we're using normals instead of tangents
  const float dot_prod = before_unit_normal.Dot(after_unit_normal);
  const AngleType angle_type = Dot2AngleType(dot_prod);
  ScalarPoint before = before_unit_normal;
  ScalarPoint after = after_unit_normal;
  ScalarPoint mid;
  float sin_half_angle;
  bool ccw;

  if (angle_type == kNearlyLine_AngleType) return;
  if (angle_type == kNearly180_AngleType) {
    curr_is_line = false;
    goto do_blunt;
  }

  ccw = !IsClockwise(before, after);
  if (ccw) {
    std::swap(outer, inner);
    before = -before;
    after = -after;
  }

  // Before we enter the world of square-roots and divides, check if we're
  // trying to join an upright right angle (common case for stroking
  // rectangles). If so, special case that (for speed an accuracy). Note: we
  // only need to check one normal if dot==0
  if (0 == dot_prod && inv_miter_limit <= kOneOverSqrt2) {
    mid = (before + after) * radius;
    goto do_miter;
  }

  // midLength = radius / sinHalfAngle
  // if (midLength > miterLimit * radius) abort
  // if (radius / sinHalf > miterLimit * radius) abort
  // if (1 / sinHalf > miterLimit) abort
  // if (1 / miterLimit > sinHalf) abort
  // My dotProd is opposite sign, since it is built from normals and not
  // tangents hence 1 + dot instead of 1 - dot in the formula
  sin_half_angle = std::sqrt((1 + dot_prod) * 0.5f);
  if (sin_half_angle < inv_miter_limit) {
    curr_is_line = false;
    goto do_blunt;
  }

  // choose the most accurate way to form the initial mid-vector
  if (angle_type == kSharp_AngleType) {
    mid = {after.y - before.y, before.x - after.x};
    if (ccw) mid = -mid;
  } else {
    mid = {before.x + after.x, before.y + after.y};
  }

  point::SetLength(&mid, radius / sin_half_angle);
do_miter:
  if (prev_is_line) outer->SetLastPt({pivot.x + mid.x, pivot.y + mid.y});
  else outer->LineTo({pivot.x + mid.x, pivot.y + mid.y});

do_blunt:
  after *= radius;
  if (!curr_is_line) outer->LineTo({pivot.x + after.x, pivot.y + after.y});
  HandleInnerJoin(inner, pivot, after);
}

CapProc CapFactory(StrokeCap cap) {
  static const CapProc kCappers[] = {ButtCapper, RoundCapper, SquareCapper};
  return kCappers[static_cast<int>(cap)];
}

JoinProc JoinFactory(StrokeJoin join) {
  static const JoinProc kJoiners[] = {MiterJoiner, RoundJoiner, BluntJoiner};
  return kJoiners[static_cast<int>(join)];
}

enum {
  kTangent_RecursiveLimit,
  kCubic_RecursiveLimit,
  kConic_RecursiveLimit,
  kQuad_RecursiveLimit
};

// quads with extreme widths (e.g. (0,1) (1,6) (0,3) width=5e7) recurse to
// point of failure largest seen for normal cubics : 5, 26 largest seen for
// normal quads : 11 3x limits seen in practice, except for cubics (3x limit
// would be ~75). For cubics, we never get close to 75 when running through
// dm. The limit of 24 was chosen because it's close to the peak in a count of
// cubic recursion depths visited (define DEBUG_CUBIC_RECURSION_DEPTHS) and no
// diffs were produced on gold when using it.
const int kRecursiveLimits[] = {5 * 3, 24, 11 * 3, 11 * 3};

bool DegenerateVector(ScalarPoint v) {
  return !point::CanNormalize(v.x, v.y);
}

bool SetNormalUnitNormal(ScalarPoint before, ScalarPoint after, float scale, float radius, ScalarPoint* normal,
                         ScalarPoint* unit_normal) {
  if (!point::SetNormalize(unit_normal, (after.x - before.x) * scale, (after.y - before.y) * scale)) return false;
  point::RotateCCW(*unit_normal, unit_normal);
  *normal = *unit_normal * radius;
  return true;
}

bool SetNormalUnitNormal(ScalarPoint vec, float radius, ScalarPoint* normal, ScalarPoint* unit_normal) {
  if (!point::SetNormalize(unit_normal, vec.x, vec.y)) return false;
  point::RotateCCW(*unit_normal, unit_normal);
  *normal = *unit_normal * radius;
  return true;
}

// SkQuadConstruct: the state of the quad stroke under construction.
struct QuadConstruct {
  ScalarPoint quad[3];        // the stroked quad parallel to the original curve
  ScalarPoint tangent_start;  // tangent vector at quad[0]
  ScalarPoint tangent_end;    // tangent vector at quad[2]
  float start_t;              // a segment of the original curve
  float mid_t;                //              "
  float end_t;                //              "
  bool start_set;             // state to share common points across structs
  bool end_set;               //                     "
  bool opposite_tangents;     // set if coincident tangents have opposite directions

  // return false if start and end are too close to have a unique middle
  bool Init(float start, float end) {
    start_t = start;
    mid_t = (start + end) * 0.5f;
    end_t = end;
    start_set = end_set = false;
    return start_t < mid_t && mid_t < end_t;
  }

  bool InitWithStart(QuadConstruct* parent) {
    if (!Init(parent->start_t, parent->mid_t)) return false;
    quad[0] = parent->quad[0];
    tangent_start = parent->tangent_start;
    start_set = true;
    return true;
  }

  bool InitWithEnd(QuadConstruct* parent) {
    if (!Init(parent->mid_t, parent->end_t)) return false;
    quad[2] = parent->quad[2];
    tangent_end = parent->tangent_end;
    end_set = true;
    return true;
  }
};

bool IsZeroLengthSincePoint(std::span<const ScalarPoint> span, int start_pt_index) {
  const int count = static_cast<int>(span.size()) - start_pt_index;
  if (count < 2) return true;
  const ScalarPoint* pts = span.data() + start_pt_index;
  const ScalarPoint first = *pts;
  for (int index = 1; index < count; ++index) {
    if (!(first == pts[index])) return false;
  }
  return true;
}

// returns the distance squared from the point to the line
float PtToLine(ScalarPoint pt, ScalarPoint line_start, ScalarPoint line_end) {
  const ScalarPoint dxy = line_end - line_start;
  const ScalarPoint ab0 = pt - line_start;
  const float numer = dxy.Dot(ab0);
  const float denom = dxy.Dot(dxy);
  const float t = numer / denom;
  if (t >= 0 && t <= 1) {
    const ScalarPoint hit = line_start * (1 - t) + line_end * t;
    return point::DistanceToSqd(hit, pt);
  }
  return point::DistanceToSqd(pt, line_start);
}

// returns the distance squared from the point to the line
float PtToTangentLine(ScalarPoint pt, ScalarPoint line_start, ScalarPoint tangent) {
  const ScalarPoint dxy = tangent;
  const ScalarPoint ab0 = pt - line_start;
  const float numer = dxy.Dot(ab0);
  const float denom = dxy.Dot(dxy);
  const float t = numer / denom;
  if (t >= 0 && t <= 1) {
    const ScalarPoint hit = line_start + tangent * t;
    return point::DistanceToSqd(hit, pt);
  }
  return point::DistanceToSqd(pt, line_start);
}

// Given a cubic, determine if all four points are in a line. Return true if
// the inner points is close to a line connecting the outermost points.
//
// Find the outermost point by looking for the largest difference in X or Y.
// Given the indices of the outermost points, and that outer_1 is greater than
// outer_2, this table shows the index of the smaller of the remaining points:
//
//                   outer_2
//               0    1    2    3
//   outer_1     ----------------
//      0     |  -    2    1    1
//      1     |  -    -    0    0
//      2     |  -    -    -    0
//      3     |  -    -    -    -
//
// If outer_1 == 0 and outer_2 == 1, the smaller of the remaining indices (2
// and 3) is 2.
//
// This table can be collapsed to: (1 + (2 >> outer_2)) >> outer_1
//
// Given three indices (outer_1 outer_2 mid_1) from 0..3, the remaining index
// is:
//
//            mid_2 == (outer_1 ^ outer_2 ^ mid_1)
bool CubicInLine(const ScalarPoint cubic[4]) {
  float pt_max = -1;
  int outer1 = 0;
  int outer2 = 0;
  for (int index = 0; index < 3; ++index) {
    for (int inner = index + 1; inner < 4; ++inner) {
      const ScalarPoint test_diff = cubic[inner] - cubic[index];
      const float test_max = std::max(std::fabs(test_diff.x), std::fabs(test_diff.y));
      if (pt_max < test_max) {
        outer1 = index;
        outer2 = inner;
        pt_max = test_max;
      }
    }
  }
  const int mid1 = (1 + (2 >> outer2)) >> outer1;
  const int mid2 = outer1 ^ outer2 ^ mid1;
  const float line_slop = pt_max * pt_max * 0.00001f; // this multiplier is pulled out of the air
  return PtToLine(cubic[mid1], cubic[outer1], cubic[outer2]) <= line_slop &&
         PtToLine(cubic[mid2], cubic[outer1], cubic[outer2]) <= line_slop;
}

// Given quad, see if all there points are in a line. Return true if the
// inside point is close to a line connecting the outermost points.
//
// Find the outermost point by looking for the largest difference in X or Y.
// Since the XOR of the indices is 3  (0 ^ 1 ^ 2) the missing index equals:
// outer_1 ^ outer_2 ^ 3
bool QuadInLine(const ScalarPoint quad[3]) {
  float pt_max = -1;
  int outer1 = 0;
  int outer2 = 0;
  for (int index = 0; index < 2; ++index) {
    for (int inner = index + 1; inner < 3; ++inner) {
      const ScalarPoint test_diff = quad[inner] - quad[index];
      const float test_max = std::max(std::fabs(test_diff.x), std::fabs(test_diff.y));
      if (pt_max < test_max) {
        outer1 = index;
        outer2 = inner;
        pt_max = test_max;
      }
    }
  }
  const int mid = outer1 ^ outer2 ^ 3;
  const float kCurvatureSlop = 0.000005f; // this multiplier is pulled out of the air
  const float line_slop = pt_max * pt_max * kCurvatureSlop;
  return PtToLine(quad[mid], quad[outer1], quad[outer2]) <= line_slop;
}

bool ConicInLine(const Conic& conic) {
  return QuadInLine(conic.pts);
}

// Intersect the line with the quad and return the t values on the quad where
// the line crosses.
int IntersectQuadRay(const ScalarPoint line[2], const ScalarPoint quad[3], float roots[2]) {
  const ScalarPoint vec = line[1] - line[0];
  float r[3];
  for (int n = 0; n < 3; ++n) r[n] = vec.Cross(quad[n] - line[0]);
  float a = r[2];
  float b = r[1];
  const float c = r[0];
  a += c - 2 * b; // A = a - 2*b + c
  b -= c;         // B = -(b - c)
  return FindUnitQuadRoots(a, 2 * b, c, roots);
}

bool PointsWithinDist(ScalarPoint near_pt, ScalarPoint far_pt, float limit) {
  return point::DistanceToSqd(near_pt, far_pt) <= limit * limit;
}

bool SharpAngle(const ScalarPoint quad[3]) {
  ScalarPoint smaller = quad[1] - quad[0];
  ScalarPoint larger = quad[1] - quad[2];
  const float smaller_len = point::LengthSqd(smaller);
  float larger_len = point::LengthSqd(larger);
  if (smaller_len > larger_len) {
    std::swap(smaller, larger);
    larger_len = smaller_len;
  }
  if (!point::SetLength(&smaller, larger_len)) return false;
  const float dot = smaller.Dot(larger);
  return dot > 0;
}

// SkPathStroker.
class PathStroker {
public:
  PathStroker(const ScalarPath& src, float radius, float miter_limit, StrokeCap cap, StrokeJoin join,
              float res_scale);

  bool HasOnlyMoveTo() const {
    return 0 == segment_count_;
  }
  ScalarPoint MoveToPt() const {
    return first_pt_;
  }

  void MoveTo(ScalarPoint);
  void LineTo(ScalarPoint, const ScalarPath::Iter* iter = nullptr);
  void QuadTo(ScalarPoint, ScalarPoint);
  void ConicTo(ScalarPoint, ScalarPoint, float weight);
  void CubicTo(ScalarPoint, ScalarPoint, ScalarPoint);
  void Close(bool is_line) {
    FinishContour(true, is_line);
  }

  void Done(ScalarPath* dst, bool is_line) {
    FinishContour(false, is_line);
    *dst = std::move(outer_);
    outer_.Reset();
  }

  bool IsCurrentContourEmpty() const {
    return IsZeroLengthSincePoint(inner_.Points(), 0) &&
           IsZeroLengthSincePoint(outer_.Points(), first_outer_pt_index_in_contour_);
  }

private:
  enum StrokeType {
    kOuter_StrokeType = 1, // use sign-opposite values later to flip perpendicular axis
    kInner_StrokeType = -1
  };

  enum ResultType {
    kSplit_ResultType,      // the caller should split the quad stroke in two
    kDegenerate_ResultType, // the caller should add a line
    kQuad_ResultType,       // the caller should (continue to try to) add a quad stroke
  };

  enum ReductionType {
    kPoint_ReductionType,       // all curve points are practically identical
    kLine_ReductionType,        // the control point is on the line between the ends
    kQuad_ReductionType,        // the control point is outside the line between the ends
    kDegenerate_ReductionType,  // the control point is on the line but outside the ends
    kDegenerate2_ReductionType, // two control points are on the line but outside ends (cubic)
    kDegenerate3_ReductionType, // three areas of max curvature found (for cubic)
  };

  enum IntersectRayType {
    kCtrlPt_RayType,
    kResultType_RayType,
  };

  ScalarPath* Sink() {
    return stroke_type_ == kOuter_StrokeType ? &outer_ : &inner_;
  }

  void AddDegenerateLine(const QuadConstruct*);
  static ReductionType CheckConicLinear(const Conic&, ScalarPoint* reduction);
  static ReductionType CheckCubicLinear(const ScalarPoint cubic[4], ScalarPoint reduction[3],
                                        const ScalarPoint** tangent_pt_ptr);
  static ReductionType CheckQuadLinear(const ScalarPoint quad[3], ScalarPoint* reduction);
  ResultType CompareQuadConic(const Conic&, QuadConstruct*) const;
  ResultType CompareQuadCubic(const ScalarPoint cubic[4], QuadConstruct*);
  ResultType CompareQuadQuad(const ScalarPoint quad[3], QuadConstruct*);
  void ConicPerpRay(const Conic&, float t, ScalarPoint* t_pt, ScalarPoint* on_pt, ScalarPoint* tangent) const;
  void ConicQuadEnds(const Conic&, QuadConstruct*) const;
  bool ConicStroke(const Conic&, QuadConstruct*);
  bool CubicMidOnLine(const ScalarPoint cubic[4], const QuadConstruct*) const;
  void CubicPerpRay(const ScalarPoint cubic[4], float t, ScalarPoint* t_pt, ScalarPoint* on_pt,
                    ScalarPoint* tangent) const;
  void CubicQuadEnds(const ScalarPoint cubic[4], QuadConstruct*);
  void CubicQuadMid(const ScalarPoint cubic[4], const QuadConstruct*, ScalarPoint* mid) const;
  bool CubicStroke(const ScalarPoint cubic[4], QuadConstruct*);
  void Init(StrokeType, QuadConstruct*, float t_start, float t_end);
  ResultType IntersectRay(QuadConstruct*, IntersectRayType) const;
  bool PtInQuadBounds(const ScalarPoint quad[3], ScalarPoint pt) const;
  void QuadPerpRay(const ScalarPoint quad[3], float t, ScalarPoint* t_pt, ScalarPoint* on_pt,
                   ScalarPoint* tangent) const;
  bool QuadStroke(const ScalarPoint quad[3], QuadConstruct*);
  void SetConicEndNormal(const Conic&, ScalarPoint normal_ab, ScalarPoint unit_normal_ab, ScalarPoint* normal_bc,
                         ScalarPoint* unit_normal_bc);
  void SetCubicEndNormal(const ScalarPoint cubic[4], ScalarPoint normal_ab, ScalarPoint unit_normal_ab,
                         ScalarPoint* normal_cd, ScalarPoint* unit_normal_cd);
  void SetQuadEndNormal(const ScalarPoint quad[3], ScalarPoint normal_ab, ScalarPoint unit_normal_ab,
                        ScalarPoint* normal_bc, ScalarPoint* unit_normal_bc);
  void SetRayPts(ScalarPoint t_pt, ScalarPoint* dxy, ScalarPoint* on_pt, ScalarPoint* tangent) const;
  ResultType StrokeCloseEnough(const ScalarPoint stroke[3], const ScalarPoint ray[2], QuadConstruct*) const;
  ResultType TangentsMeet(const ScalarPoint cubic[4], QuadConstruct*);

  void FinishContour(bool close, bool is_line);
  bool PreJoinTo(ScalarPoint, ScalarPoint* normal, ScalarPoint* unit_normal, bool is_line);
  void PostJoinTo(ScalarPoint, ScalarPoint normal, ScalarPoint unit_normal);

  void LineToInternal(ScalarPoint curr_pt, ScalarPoint normal);

  float radius_;
  float inv_miter_limit_;
  float res_scale_;
  float inv_res_scale_;
  float inv_res_scale_squared_;

  ScalarPoint first_normal_, prev_normal_, first_unit_normal_, prev_unit_normal_;
  ScalarPoint first_pt_, prev_pt_; // on original path
  ScalarPoint first_outer_pt_;
  int first_outer_pt_index_in_contour_;
  int segment_count_;
  bool prev_is_line_;

  CapProc capper_;
  JoinProc joiner_;

  ScalarPath inner_, outer_, cusper_; // outer is our working answer, inner is temp

  StrokeType stroke_type_ = kOuter_StrokeType;

  int recursion_depth_; // track stack depth to abort if numerics run amok
  bool found_tangents_ = false;  // do less work until tangents meet (cubic)
  bool join_completed_ = false;  // previous join was not degenerate
};

PathStroker::PathStroker(const ScalarPath&, float radius, float miter_limit, StrokeCap cap, StrokeJoin join,
                         float res_scale)
    : radius_(radius),
      res_scale_(res_scale) {
  // This is only used when join is miter_join, but we initialize it here so
  // that it is always defined, to fis valgrind warnings.
  inv_miter_limit_ = 0;

  if (join == StrokeJoin::kMiter) {
    if (miter_limit <= 1) join = StrokeJoin::kBevel;
    else inv_miter_limit_ = 1 / miter_limit;
  }
  capper_ = CapFactory(cap);
  joiner_ = JoinFactory(join);
  segment_count_ = -1;
  first_outer_pt_index_in_contour_ = 0;
  prev_is_line_ = false;

  // TODO : write a common error function used by stroking and filling
  // The '4' below matches the fill scan converter's error term
  inv_res_scale_ = 1 / (res_scale * 4);
  inv_res_scale_squared_ = inv_res_scale_ * inv_res_scale_;
  recursion_depth_ = 0;
}

bool PathStroker::PreJoinTo(ScalarPoint curr_pt, ScalarPoint* normal, ScalarPoint* unit_normal, bool curr_is_line) {
  if (!SetNormalUnitNormal(prev_pt_, curr_pt, res_scale_, radius_, normal, unit_normal)) {
    if (CapFactory(StrokeCap::kButt) == capper_) return false;
    // Square caps and round caps draw even if the segment length is zero.
    // Since the zero length segment has no direction, set the orientation to
    // upright as the default orientation
    *normal = {radius_, 0};
    *unit_normal = {1, 0};
  }

  if (segment_count_ == 0) {
    first_normal_ = *normal;
    first_unit_normal_ = *unit_normal;
    first_outer_pt_ = prev_pt_ + *normal;

    outer_.MoveTo(first_outer_pt_);
    inner_.MoveTo(prev_pt_ - *normal);
  } else { // we have a previous segment
    joiner_(&outer_, &inner_, prev_unit_normal_, prev_pt_, *unit_normal, radius_, inv_miter_limit_, prev_is_line_,
            curr_is_line);
  }
  prev_is_line_ = curr_is_line;
  return true;
}

void PathStroker::PostJoinTo(ScalarPoint curr_pt, ScalarPoint normal, ScalarPoint unit_normal) {
  join_completed_ = true;
  prev_pt_ = curr_pt;
  prev_unit_normal_ = unit_normal;
  prev_normal_ = normal;
  segment_count_ += 1;
}

void PathStroker::FinishContour(bool close, bool curr_is_line) {
  if (segment_count_ > 0) {
    if (close) {
      joiner_(&outer_, &inner_, prev_unit_normal_, prev_pt_, first_unit_normal_, radius_, inv_miter_limit_,
              prev_is_line_, curr_is_line);
      outer_.Close();

      // now add inner_ as its own contour
      if (auto pt = inner_.GetLastPt()) {
        outer_.MoveTo(*pt);
        ScalarPath inner = std::move(inner_);
        inner_.Reset();
        outer_.ReversePathTo(inner);
        outer_.Close();
      }
    } else { // add caps to start and end
      // cap the end
      if (auto pt = inner_.GetLastPt()) {
        capper_(&outer_, prev_pt_, prev_normal_, *pt, curr_is_line);
        ScalarPath inner = std::move(inner_);
        inner_.Reset();
        outer_.ReversePathTo(inner);
        // cap the start
        capper_(&outer_, first_pt_, -first_normal_, first_outer_pt_, prev_is_line_);
        outer_.Close();
      }
    }
    if (!cusper_.IsEmpty()) {
      outer_.AddPath(cusper_);
      cusper_.Reset();
    }
  }
  inner_.Reset();
  segment_count_ = -1;
  first_outer_pt_index_in_contour_ = outer_.CountPoints();
}

void PathStroker::MoveTo(ScalarPoint pt) {
  if (segment_count_ > 0) FinishContour(false, false);
  segment_count_ = 0;
  first_pt_ = prev_pt_ = pt;
  join_completed_ = false;
}

void PathStroker::LineToInternal(ScalarPoint curr_pt, ScalarPoint normal) {
  outer_.LineTo(curr_pt + normal);
  inner_.LineTo(curr_pt - normal);
}

bool HasValidTangent(const ScalarPath::Iter* iter) {
  ScalarPath::Iter copy = *iter;
  ScalarPoint pts[4];
  while (const auto verb = copy.Next(pts)) {
    switch (*verb) {
    case ScalarPath::Verb::kMove:
      return false;
    case ScalarPath::Verb::kLine:
      if (pts[0] == pts[1]) continue;
      return true;
    case ScalarPath::Verb::kQuad:
    case ScalarPath::Verb::kConic:
      if (pts[0] == pts[1] && pts[0] == pts[2]) continue;
      return true;
    case ScalarPath::Verb::kCubic:
      if (pts[0] == pts[1] && pts[0] == pts[2] && pts[0] == pts[3]) continue;
      return true;
    case ScalarPath::Verb::kClose:
      return false;
    }
  }
  return false;
}

void PathStroker::LineTo(ScalarPoint curr_pt, const ScalarPath::Iter* iter) {
  const bool teeny_line = point::EqualsWithinTolerance(prev_pt_, curr_pt, kScalarNearlyZero * inv_res_scale_);
  if (CapFactory(StrokeCap::kButt) == capper_ && teeny_line) return;
  if (teeny_line && (join_completed_ || (iter && HasValidTangent(iter)))) return;
  ScalarPoint normal, unit_normal;
  if (!PreJoinTo(curr_pt, &normal, &unit_normal, true)) return;
  LineToInternal(curr_pt, normal);
  PostJoinTo(curr_pt, normal, unit_normal);
}

void PathStroker::SetQuadEndNormal(const ScalarPoint quad[3], ScalarPoint normal_ab, ScalarPoint unit_normal_ab,
                                   ScalarPoint* normal_bc, ScalarPoint* unit_normal_bc) {
  if (!SetNormalUnitNormal(quad[1], quad[2], res_scale_, radius_, normal_bc, unit_normal_bc)) {
    *normal_bc = normal_ab;
    *unit_normal_bc = unit_normal_ab;
  }
}

void PathStroker::SetConicEndNormal(const Conic& conic, ScalarPoint normal_ab, ScalarPoint unit_normal_ab,
                                    ScalarPoint* normal_bc, ScalarPoint* unit_normal_bc) {
  SetQuadEndNormal(conic.pts, normal_ab, unit_normal_ab, normal_bc, unit_normal_bc);
}

void PathStroker::SetCubicEndNormal(const ScalarPoint cubic[4], ScalarPoint normal_ab, ScalarPoint unit_normal_ab,
                                    ScalarPoint* normal_cd, ScalarPoint* unit_normal_cd) {
  ScalarPoint ab = cubic[1] - cubic[0];
  ScalarPoint cd = cubic[3] - cubic[2];

  bool degenerate_ab = DegenerateVector(ab);
  bool degenerate_cd = DegenerateVector(cd);

  if (degenerate_ab && degenerate_cd) goto degenerate_normal;

  if (degenerate_ab) {
    ab = cubic[2] - cubic[0];
    degenerate_ab = DegenerateVector(ab);
  }
  if (degenerate_cd) {
    cd = cubic[3] - cubic[1];
    degenerate_cd = DegenerateVector(cd);
  }
  if (degenerate_ab || degenerate_cd) {
  degenerate_normal:
    *normal_cd = normal_ab;
    *unit_normal_cd = unit_normal_ab;
    return;
  }
  SetNormalUnitNormal(cd, radius_, normal_cd, unit_normal_cd);
}

void PathStroker::Init(StrokeType stroke_type, QuadConstruct* quad_pts, float t_start, float t_end) {
  stroke_type_ = stroke_type;
  found_tangents_ = false;
  quad_pts->Init(t_start, t_end);
}

PathStroker::ReductionType PathStroker::CheckCubicLinear(const ScalarPoint cubic[4], ScalarPoint reduction[3],
                                                         const ScalarPoint** tangent_pt_ptr) {
  const bool degenerate_ab = DegenerateVector(cubic[1] - cubic[0]);
  const bool degenerate_bc = DegenerateVector(cubic[2] - cubic[1]);
  const bool degenerate_cd = DegenerateVector(cubic[3] - cubic[2]);
  if (degenerate_ab & degenerate_bc & degenerate_cd) return kPoint_ReductionType;
  if (degenerate_ab + degenerate_bc + degenerate_cd == 2) return kLine_ReductionType;
  if (!CubicInLine(cubic)) {
    *tangent_pt_ptr = degenerate_ab ? &cubic[2] : &cubic[1];
    return kQuad_ReductionType;
  }
  float t_values[3];
  const int count = FindCubicMaxCurvature(cubic, t_values);
  int r_count = 0;
  // Now loop over the t-values, and reject any that evaluate to either
  // end-point
  for (int index = 0; index < count; ++index) {
    const float t = t_values[index];
    if (0 >= t || t >= 1) continue;
    EvalCubicAt(cubic, t, &reduction[r_count], nullptr, nullptr);
    if (!(reduction[r_count] == cubic[0]) && !(reduction[r_count] == cubic[3])) ++r_count;
  }
  if (r_count == 0) return kLine_ReductionType;
  return static_cast<ReductionType>(kQuad_ReductionType + r_count);
}

PathStroker::ReductionType PathStroker::CheckConicLinear(const Conic& conic, ScalarPoint* reduction) {
  const bool degenerate_ab = DegenerateVector(conic.pts[1] - conic.pts[0]);
  const bool degenerate_bc = DegenerateVector(conic.pts[2] - conic.pts[1]);
  if (degenerate_ab & degenerate_bc) return kPoint_ReductionType;
  if (degenerate_ab | degenerate_bc) return kLine_ReductionType;
  if (!ConicInLine(conic)) return kQuad_ReductionType;
  // SkFindConicMaxCurvature would be a better solution, once we know how to
  // implement it. Quad curvature is a reasonable substitute
  const float t = FindQuadMaxCurvature(conic.pts);
  if (0 == t || std::isnan(t)) return kLine_ReductionType;
  conic.EvalAt(t, reduction, nullptr);
  return kDegenerate_ReductionType;
}

PathStroker::ReductionType PathStroker::CheckQuadLinear(const ScalarPoint quad[3], ScalarPoint* reduction) {
  const bool degenerate_ab = DegenerateVector(quad[1] - quad[0]);
  const bool degenerate_bc = DegenerateVector(quad[2] - quad[1]);
  if (degenerate_ab & degenerate_bc) return kPoint_ReductionType;
  if (degenerate_ab | degenerate_bc) return kLine_ReductionType;
  if (!QuadInLine(quad)) return kQuad_ReductionType;
  const float t = FindQuadMaxCurvature(quad);
  if (0 == t || 1 == t) return kLine_ReductionType;
  *reduction = EvalQuadAt(quad, t);
  return kDegenerate_ReductionType;
}

void PathStroker::ConicTo(ScalarPoint pt1, ScalarPoint pt2, float weight) {
  const Conic conic(prev_pt_, pt1, pt2, weight);
  ScalarPoint reduction;
  const ReductionType reduction_type = CheckConicLinear(conic, &reduction);
  if (kPoint_ReductionType == reduction_type) {
    // If the stroke consists of a moveTo followed by a degenerate curve,
    // treat it as if it were followed by a zero-length line. Lines without
    // length can have square and round end caps.
    LineTo(pt2);
    return;
  }
  if (kLine_ReductionType == reduction_type) {
    LineTo(pt2);
    return;
  }
  if (kDegenerate_ReductionType == reduction_type) {
    LineTo(reduction);
    const JoinProc save_joiner = joiner_;
    joiner_ = JoinFactory(StrokeJoin::kRound);
    LineTo(pt2);
    joiner_ = save_joiner;
    return;
  }
  ScalarPoint normal_ab, unit_ab, normal_bc, unit_bc;
  if (!PreJoinTo(pt1, &normal_ab, &unit_ab, false)) {
    LineTo(pt2);
    return;
  }
  QuadConstruct quad_pts;
  Init(kOuter_StrokeType, &quad_pts, 0, 1);
  (void)ConicStroke(conic, &quad_pts);
  Init(kInner_StrokeType, &quad_pts, 0, 1);
  (void)ConicStroke(conic, &quad_pts);
  SetConicEndNormal(conic, normal_ab, unit_ab, &normal_bc, &unit_bc);
  PostJoinTo(pt2, normal_bc, unit_bc);
}

void PathStroker::QuadTo(ScalarPoint pt1, ScalarPoint pt2) {
  const ScalarPoint quad[3] = {prev_pt_, pt1, pt2};
  ScalarPoint reduction;
  const ReductionType reduction_type = CheckQuadLinear(quad, &reduction);
  if (kPoint_ReductionType == reduction_type) {
    // If the stroke consists of a moveTo followed by a degenerate curve,
    // treat it as if it were followed by a zero-length line. Lines without
    // length can have square and round end caps.
    LineTo(pt2);
    return;
  }
  if (kLine_ReductionType == reduction_type) {
    LineTo(pt2);
    return;
  }
  if (kDegenerate_ReductionType == reduction_type) {
    LineTo(reduction);
    const JoinProc save_joiner = joiner_;
    joiner_ = JoinFactory(StrokeJoin::kRound);
    LineTo(pt2);
    joiner_ = save_joiner;
    return;
  }
  ScalarPoint normal_ab, unit_ab, normal_bc, unit_bc;
  if (!PreJoinTo(pt1, &normal_ab, &unit_ab, false)) {
    LineTo(pt2);
    return;
  }
  QuadConstruct quad_pts;
  Init(kOuter_StrokeType, &quad_pts, 0, 1);
  (void)QuadStroke(quad, &quad_pts);
  Init(kInner_StrokeType, &quad_pts, 0, 1);
  (void)QuadStroke(quad, &quad_pts);
  SetQuadEndNormal(quad, normal_ab, unit_ab, &normal_bc, &unit_bc);
  PostJoinTo(pt2, normal_bc, unit_bc);
}

// Given a point on the curve and its derivative, scale the derivative by the
// radius, and compute the perpendicular point and its tangent.
void PathStroker::SetRayPts(ScalarPoint t_pt, ScalarPoint* dxy, ScalarPoint* on_pt, ScalarPoint* tangent) const {
  if (!point::SetLength(dxy, radius_)) *dxy = {radius_, 0};
  const float axis_flip = static_cast<float>(stroke_type_); // go opposite ways for outer, inner
  on_pt->x = t_pt.x + axis_flip * dxy->y;
  on_pt->y = t_pt.y - axis_flip * dxy->x;
  if (tangent) *tangent = *dxy;
}

// Given a conic and t, return the point on curve, its perpendicular, and the
// perpendicular tangent. Returns false if the perpendicular could not be
// computed (because the derivative collapsed to 0)
void PathStroker::ConicPerpRay(const Conic& conic, float t, ScalarPoint* t_pt, ScalarPoint* on_pt,
                               ScalarPoint* tangent) const {
  ScalarPoint dxy;
  conic.EvalAt(t, t_pt, &dxy);
  if (dxy.IsZero()) dxy = conic.pts[2] - conic.pts[0];
  SetRayPts(*t_pt, &dxy, on_pt, tangent);
}

// Given a conic and a t range, find the start and end if they haven't been
// found already.
void PathStroker::ConicQuadEnds(const Conic& conic, QuadConstruct* quad_pts) const {
  if (!quad_pts->start_set) {
    ScalarPoint conic_start_pt;
    ConicPerpRay(conic, quad_pts->start_t, &conic_start_pt, &quad_pts->quad[0], &quad_pts->tangent_start);
    quad_pts->start_set = true;
  }
  if (!quad_pts->end_set) {
    ScalarPoint conic_end_pt;
    ConicPerpRay(conic, quad_pts->end_t, &conic_end_pt, &quad_pts->quad[2], &quad_pts->tangent_end);
    quad_pts->end_set = true;
  }
}

// Given a cubic and t, return the point on curve, its perpendicular, and the
// perpendicular tangent.
void PathStroker::CubicPerpRay(const ScalarPoint cubic[4], float t, ScalarPoint* t_pt, ScalarPoint* on_pt,
                               ScalarPoint* tangent) const {
  ScalarPoint dxy;
  ScalarPoint chopped[7];
  EvalCubicAt(cubic, t, t_pt, &dxy, nullptr);
  if (dxy.IsZero()) {
    const ScalarPoint* c_pts = cubic;
    if (point::ScalarNearlyZero(t)) {
      dxy = cubic[2] - cubic[0];
    } else if (point::ScalarNearlyZero(1 - t)) {
      dxy = cubic[3] - cubic[1];
    } else {
      // If the cubic inflection falls on the cusp, subdivide the cubic to
      // find the tangent at that point.
      ChopCubicAt(cubic, chopped, t);
      dxy = chopped[3] - chopped[2];
      if (dxy.IsZero()) {
        dxy = chopped[3] - chopped[1];
        c_pts = chopped;
      }
    }
    if (dxy.IsZero()) dxy = c_pts[3] - c_pts[0];
  }
  SetRayPts(*t_pt, &dxy, on_pt, tangent);
}

// Given a cubic and a t range, find the start and end if they haven't been
// found already.
void PathStroker::CubicQuadEnds(const ScalarPoint cubic[4], QuadConstruct* quad_pts) {
  if (!quad_pts->start_set) {
    ScalarPoint cubic_start_pt;
    CubicPerpRay(cubic, quad_pts->start_t, &cubic_start_pt, &quad_pts->quad[0], &quad_pts->tangent_start);
    quad_pts->start_set = true;
  }
  if (!quad_pts->end_set) {
    ScalarPoint cubic_end_pt;
    CubicPerpRay(cubic, quad_pts->end_t, &cubic_end_pt, &quad_pts->quad[2], &quad_pts->tangent_end);
    quad_pts->end_set = true;
  }
}

void PathStroker::CubicQuadMid(const ScalarPoint cubic[4], const QuadConstruct* quad_pts, ScalarPoint* mid) const {
  ScalarPoint cubic_mid_pt;
  CubicPerpRay(cubic, quad_pts->mid_t, &cubic_mid_pt, mid, nullptr);
}

// Given a quad and t, return the point on curve, its perpendicular, and the
// perpendicular tangent.
void PathStroker::QuadPerpRay(const ScalarPoint quad[3], float t, ScalarPoint* t_pt, ScalarPoint* on_pt,
                              ScalarPoint* tangent) const {
  ScalarPoint dxy;
  EvalQuadAt(quad, t, t_pt, &dxy);
  if (dxy.IsZero()) dxy = quad[2] - quad[0];
  SetRayPts(*t_pt, &dxy, on_pt, tangent);
}

// Find the intersection of the stroke tangents to construct a stroke quad.
// Return whether the stroke is a degenerate (a line), a quad, or must be
// split. Optionally compute the quad's control point.
PathStroker::ResultType PathStroker::IntersectRay(QuadConstruct* quad_pts, IntersectRayType intersect_ray_type) const {
  const ScalarPoint start = quad_pts->quad[0];
  const ScalarPoint end = quad_pts->quad[2];
  const ScalarPoint a_len = quad_pts->tangent_start;
  const ScalarPoint b_len = quad_pts->tangent_end;
  // Slopes match when denom goes to zero:
  //                   axLen / ayLen ==                   bxLen / byLen
  // (ayLen * byLen) * axLen / ayLen == (ayLen * byLen) * bxLen / byLen
  //          byLen  * axLen         ==  ayLen          * bxLen
  //          byLen  * axLen         -   ayLen          * bxLen         ( == denom )
  const float denom = a_len.Cross(b_len);
  if (denom == 0 || !std::isfinite(denom)) {
    quad_pts->opposite_tangents = a_len.Dot(b_len) < 0;
    return kDegenerate_ResultType;
  }
  quad_pts->opposite_tangents = false;
  const ScalarPoint ab0 = start - end;
  float numer_a = b_len.Cross(ab0);
  const float numer_b = a_len.Cross(ab0);
  if ((numer_a >= 0) == (numer_b >= 0)) { // if the control point is outside the quad ends
    // if the perpendicular distances from the quad points to the opposite
    // tangent line are small, a straight line is good enough
    const float dist1 = PtToTangentLine(start, end, quad_pts->tangent_end);
    const float dist2 = PtToTangentLine(end, start, quad_pts->tangent_start);
    if (std::max(dist1, dist2) <= inv_res_scale_squared_) return kDegenerate_ResultType;
    return kSplit_ResultType;
  }
  // check to see if the denominator is teeny relative to the numerator
  // if the offset by one will be lost, the ratio is too large
  numer_a /= denom;
  const bool valid_divide = numer_a > numer_a - 1;
  if (valid_divide) {
    if (kCtrlPt_RayType == intersect_ray_type) {
      ScalarPoint* ctrl_pt = &quad_pts->quad[1];
      // the intersection of the tangents need not be on the tangent segment
      // so 0 <= numerA <= 1 is not necessarily true
      *ctrl_pt = start + quad_pts->tangent_start * numer_a;
    }
    return kQuad_ResultType;
  }
  quad_pts->opposite_tangents = a_len.Dot(b_len) < 0;
  // if the lines are parallel, straight line is good enough
  return kDegenerate_ResultType;
}

// Given a cubic and a t-range, determine if the stroke can be described by a
// quadratic.
PathStroker::ResultType PathStroker::TangentsMeet(const ScalarPoint cubic[4], QuadConstruct* quad_pts) {
  CubicQuadEnds(cubic, quad_pts);
  return IntersectRay(quad_pts, kResultType_RayType);
}

// Return true if the point is close to the bounds of the quad. This is used
// as a quick reject.
bool PathStroker::PtInQuadBounds(const ScalarPoint quad[3], ScalarPoint pt) const {
  const float x_min = std::min({quad[0].x, quad[1].x, quad[2].x});
  if (pt.x + inv_res_scale_ < x_min) return false;
  const float x_max = std::max({quad[0].x, quad[1].x, quad[2].x});
  if (pt.x - inv_res_scale_ > x_max) return false;
  const float y_min = std::min({quad[0].y, quad[1].y, quad[2].y});
  if (pt.y + inv_res_scale_ < y_min) return false;
  const float y_max = std::max({quad[0].y, quad[1].y, quad[2].y});
  if (pt.y - inv_res_scale_ > y_max) return false;
  return true;
}

PathStroker::ResultType PathStroker::StrokeCloseEnough(const ScalarPoint stroke[3], const ScalarPoint ray[2],
                                                       QuadConstruct* quad_pts) const {
  const ScalarPoint stroke_mid = EvalQuadAt(stroke, 0.5f);
  // measure the distance from the curve to the quad-stroke midpoint, compare
  // to radius
  if (PointsWithinDist(ray[0], stroke_mid, inv_res_scale_)) { // if the difference is small
    if (SharpAngle(quad_pts->quad)) return kSplit_ResultType;
    return kQuad_ResultType;
  }
  // measure the distance to quad's bounds (quick reject)
  // an alternative : look for point in triangle
  if (!PtInQuadBounds(stroke, ray[0])) return kSplit_ResultType; // if far, subdivide
  // measure the curve ray distance to the quad-stroke
  float roots[2];
  const int root_count = IntersectQuadRay(ray, stroke, roots);
  if (root_count != 1) return kSplit_ResultType;
  const ScalarPoint quad_pt = EvalQuadAt(stroke, roots[0]);
  const float error = inv_res_scale_ * (1 - std::fabs(roots[0] - 0.5f) * 2);
  if (PointsWithinDist(ray[0], quad_pt, error)) { // if the difference is small, we're done
    if (SharpAngle(quad_pts->quad)) return kSplit_ResultType;
    return kQuad_ResultType;
  }
  // otherwise, subdivide
  return kSplit_ResultType;
}

PathStroker::ResultType PathStroker::CompareQuadCubic(const ScalarPoint cubic[4], QuadConstruct* quad_pts) {
  // get the quadratic approximation of the stroke
  CubicQuadEnds(cubic, quad_pts);
  const ResultType result_type = IntersectRay(quad_pts, kCtrlPt_RayType);
  if (result_type != kQuad_ResultType) return result_type;
  // project a ray from the curve to the stroke
  ScalarPoint ray[2]; // points near midpoint on quad, midpoint on cubic
  CubicPerpRay(cubic, quad_pts->mid_t, &ray[1], &ray[0], nullptr);
  return StrokeCloseEnough(quad_pts->quad, ray, quad_pts);
}

PathStroker::ResultType PathStroker::CompareQuadConic(const Conic& conic, QuadConstruct* quad_pts) const {
  // get the quadratic approximation of the stroke
  ConicQuadEnds(conic, quad_pts);
  const ResultType result_type = IntersectRay(quad_pts, kCtrlPt_RayType);
  if (result_type != kQuad_ResultType) return result_type;
  // project a ray from the curve to the stroke
  ScalarPoint ray[2]; // points near midpoint on quad, midpoint on conic
  ConicPerpRay(conic, quad_pts->mid_t, &ray[1], &ray[0], nullptr);
  return StrokeCloseEnough(quad_pts->quad, ray, quad_pts);
}

PathStroker::ResultType PathStroker::CompareQuadQuad(const ScalarPoint quad[3], QuadConstruct* quad_pts) {
  // get the quadratic approximation of the stroke
  if (!quad_pts->start_set) {
    ScalarPoint quad_start_pt;
    QuadPerpRay(quad, quad_pts->start_t, &quad_start_pt, &quad_pts->quad[0], &quad_pts->tangent_start);
    quad_pts->start_set = true;
  }
  if (!quad_pts->end_set) {
    ScalarPoint quad_end_pt;
    QuadPerpRay(quad, quad_pts->end_t, &quad_end_pt, &quad_pts->quad[2], &quad_pts->tangent_end);
    quad_pts->end_set = true;
  }
  const ResultType result_type = IntersectRay(quad_pts, kCtrlPt_RayType);
  if (result_type != kQuad_ResultType) return result_type;
  // project a ray from the curve to the stroke
  ScalarPoint ray[2];
  QuadPerpRay(quad, quad_pts->mid_t, &ray[1], &ray[0], nullptr);
  return StrokeCloseEnough(quad_pts->quad, ray, quad_pts);
}

void PathStroker::AddDegenerateLine(const QuadConstruct* quad_pts) {
  Sink()->LineTo(quad_pts->quad[2]);
}

bool PathStroker::CubicMidOnLine(const ScalarPoint cubic[4], const QuadConstruct* quad_pts) const {
  ScalarPoint stroke_mid;
  CubicQuadMid(cubic, quad_pts, &stroke_mid);
  const float dist = PtToLine(stroke_mid, quad_pts->quad[0], quad_pts->quad[2]);
  return dist < inv_res_scale_squared_;
}

bool PathStroker::CubicStroke(const ScalarPoint cubic[4], QuadConstruct* quad_pts) {
  if (!found_tangents_) {
    const ResultType result_type = TangentsMeet(cubic, quad_pts);
    if (kQuad_ResultType != result_type) {
      if ((kDegenerate_ResultType == result_type ||
           PointsWithinDist(quad_pts->quad[0], quad_pts->quad[2], inv_res_scale_)) &&
          CubicMidOnLine(cubic, quad_pts)) {
        AddDegenerateLine(quad_pts);
        return true;
      }
    } else {
      found_tangents_ = true;
    }
  }
  if (found_tangents_) {
    const ResultType result_type = CompareQuadCubic(cubic, quad_pts);
    if (kQuad_ResultType == result_type) {
      const ScalarPoint* stroke = quad_pts->quad;
      Sink()->QuadTo(stroke[1], stroke[2]);
      return true;
    }
    if (kDegenerate_ResultType == result_type) {
      if (!quad_pts->opposite_tangents) {
        AddDegenerateLine(quad_pts);
        return true;
      }
    }
  }
  if (!quad_pts->quad[2].IsFinite()) return false; // just abort if projected quad isn't representable
  if (++recursion_depth_ > kRecursiveLimits[found_tangents_]) {
    // If we stop making progress, just emit a line and move on
    AddDegenerateLine(quad_pts);
    return true;
  }
  QuadConstruct half;
  if (!half.InitWithStart(quad_pts)) {
    AddDegenerateLine(quad_pts);
    --recursion_depth_;
    return true;
  }
  if (!CubicStroke(cubic, &half)) return false;
  if (!half.InitWithEnd(quad_pts)) {
    AddDegenerateLine(quad_pts);
    --recursion_depth_;
    return true;
  }
  if (!CubicStroke(cubic, &half)) return false;
  --recursion_depth_;
  return true;
}

bool PathStroker::ConicStroke(const Conic& conic, QuadConstruct* quad_pts) {
  const ResultType result_type = CompareQuadConic(conic, quad_pts);
  if (kQuad_ResultType == result_type) {
    const ScalarPoint* stroke = quad_pts->quad;
    Sink()->QuadTo(stroke[1], stroke[2]);
    return true;
  }
  if (kDegenerate_ResultType == result_type) {
    AddDegenerateLine(quad_pts);
    return true;
  }
  if (++recursion_depth_ > kRecursiveLimits[kConic_RecursiveLimit]) {
    // If we stop making progress, just emit a line and move on
    AddDegenerateLine(quad_pts);
    return true;
  }
  QuadConstruct half;
  (void)half.InitWithStart(quad_pts);
  if (!ConicStroke(conic, &half)) return false;
  (void)half.InitWithEnd(quad_pts);
  if (!ConicStroke(conic, &half)) return false;
  --recursion_depth_;
  return true;
}

bool PathStroker::QuadStroke(const ScalarPoint quad[3], QuadConstruct* quad_pts) {
  const ResultType result_type = CompareQuadQuad(quad, quad_pts);
  if (kQuad_ResultType == result_type) {
    const ScalarPoint* stroke = quad_pts->quad;
    Sink()->QuadTo(stroke[1], stroke[2]);
    return true;
  }
  if (kDegenerate_ResultType == result_type) {
    AddDegenerateLine(quad_pts);
    return true;
  }
  if (++recursion_depth_ > kRecursiveLimits[kQuad_RecursiveLimit]) {
    // If we stop making progress, just emit a line and move on
    AddDegenerateLine(quad_pts);
    return true;
  }
  QuadConstruct half;
  (void)half.InitWithStart(quad_pts);
  if (!QuadStroke(quad, &half)) return false;
  (void)half.InitWithEnd(quad_pts);
  if (!QuadStroke(quad, &half)) return false;
  --recursion_depth_;
  return true;
}

void PathStroker::CubicTo(ScalarPoint pt1, ScalarPoint pt2, ScalarPoint pt3) {
  const ScalarPoint cubic[4] = {prev_pt_, pt1, pt2, pt3};
  ScalarPoint reduction[3];
  const ScalarPoint* tangent_pt = nullptr;
  const ReductionType reduction_type = CheckCubicLinear(cubic, reduction, &tangent_pt);
  if (kPoint_ReductionType == reduction_type) {
    // If the stroke consists of a moveTo followed by a degenerate curve,
    // treat it as if it were followed by a zero-length line. Lines without
    // length can have square and round end caps.
    LineTo(pt3);
    return;
  }
  if (kLine_ReductionType == reduction_type) {
    LineTo(pt3);
    return;
  }
  if (kDegenerate_ReductionType <= reduction_type && kDegenerate3_ReductionType >= reduction_type) {
    LineTo(reduction[0]);
    const JoinProc save_joiner = joiner_;
    joiner_ = JoinFactory(StrokeJoin::kRound);
    if (kDegenerate2_ReductionType <= reduction_type) LineTo(reduction[1]);
    if (kDegenerate3_ReductionType == reduction_type) LineTo(reduction[2]);
    LineTo(pt3);
    joiner_ = save_joiner;
    return;
  }
  ScalarPoint normal_ab, unit_ab, normal_cd, unit_cd;
  if (!PreJoinTo(*tangent_pt, &normal_ab, &unit_ab, false)) {
    LineTo(pt3);
    return;
  }
  float t_values[2];
  const int count = FindCubicInflections(cubic, t_values);
  float last_t = 0;
  for (int index = 0; index <= count; ++index) {
    const float next_t = index < count ? t_values[index] : 1;
    QuadConstruct quad_pts;
    Init(kOuter_StrokeType, &quad_pts, last_t, next_t);
    (void)CubicStroke(cubic, &quad_pts);
    Init(kInner_StrokeType, &quad_pts, last_t, next_t);
    (void)CubicStroke(cubic, &quad_pts);
    last_t = next_t;
  }
  const float cusp = FindCubicCusp(cubic);
  if (cusp > 0) {
    ScalarPoint cusp_loc;
    EvalCubicAt(cubic, cusp, &cusp_loc, nullptr, nullptr);
    cusper_.AddCircle(cusp_loc.x, cusp_loc.y, radius_);
  }
  // emit the join even if one stroke succeeded but the last one failed
  // this avoids reversing an inner stroke with a partial path followed by
  // another moveto
  SetCubicEndNormal(cubic, normal_ab, unit_ab, &normal_cd, &unit_cd);
  PostJoinTo(pt3, normal_cd, unit_cd);
}

PathDirection ReverseDirection(PathDirection dir) {
  return dir == PathDirection::kCW ? PathDirection::kCCW : PathDirection::kCW;
}

void AddBevel(ScalarPath* path, const ScalarRect& r, const ScalarRect& outer, PathDirection dir) {
  ScalarPoint pts[8];
  if (PathDirection::kCW == dir) {
    pts[0] = {r.left, outer.top};
    pts[1] = {r.right, outer.top};
    pts[2] = {outer.right, r.top};
    pts[3] = {outer.right, r.bottom};
    pts[4] = {r.right, outer.bottom};
    pts[5] = {r.left, outer.bottom};
    pts[6] = {outer.left, r.bottom};
    pts[7] = {outer.left, r.top};
  } else {
    pts[7] = {r.left, outer.top};
    pts[6] = {r.right, outer.top};
    pts[5] = {outer.right, r.top};
    pts[4] = {outer.right, r.bottom};
    pts[3] = {r.right, outer.bottom};
    pts[2] = {r.left, outer.bottom};
    pts[1] = {outer.left, r.bottom};
    pts[0] = {outer.left, r.top};
  }
  path->AddPolygon(pts, true);
}

} // namespace

// must be < 0, since ==0 means hairline, and >0 means normal stroke
constexpr float kStrokeRec_FillStyleWidth = -1;

StrokeRec::StrokeRec(InitStyle s)
    : res_scale_(1),
      width_((kFill_InitStyle == s) ? kStrokeRec_FillStyleWidth : 0),
      miter_limit_(kDefaultMiterLimit),
      cap_(StrokeCap::kButt),
      join_(StrokeJoin::kMiter) {
}

StrokeRec::StrokeRec(const PlatformPaint& paint, float res_scale)
    : res_scale_(res_scale) {
  switch (paint.GetStyle()) {
  case PlatformPaint::Style::kFill:
    width_ = kStrokeRec_FillStyleWidth;
    break;
  case PlatformPaint::Style::kStroke:
    width_ = paint.GetStrokeWidth();
    break;
  }
  // copy these from the paint, regardless of our "style"
  miter_limit_ = paint.GetStrokeMiter();
  cap_ = paint.GetStrokeCap();
  join_ = paint.GetStrokeJoin();
}

StrokeRec::Style StrokeRec::GetStyle() const {
  if (width_ < 0) return kFill_Style;
  if (0 == width_) return kHairline_Style;
  return kStroke_Style;
}

void StrokeRec::SetFillStyle() {
  width_ = kStrokeRec_FillStyleWidth;
}

void StrokeRec::SetHairlineStyle() {
  width_ = 0;
}

void StrokeRec::SetStrokeStyle(float width) {
  width_ = width;
}

bool StrokeRec::ApplyToPath(ScalarPath* dst, const ScalarPath& src) const {
  if (width_ <= 0) return false; // hairline or fill
  Stroke stroker;
  stroker.SetCap(cap_);
  stroker.SetJoin(join_);
  stroker.SetMiterLimit(miter_limit_);
  stroker.SetWidth(width_);
  stroker.SetResScale(res_scale_);
  stroker.StrokePath(src, dst);
  return true;
}

float StrokeRec::GetInflationRadius() const {
  return GetInflationRadius(join_, miter_limit_, cap_, width_);
}

float StrokeRec::GetInflationRadius(StrokeJoin join, float miter_limit, StrokeCap cap, float stroke_width) {
  if (stroke_width < 0) return 0; // fill
  if (0 == stroke_width) {
    // FIXME: We need a "matrixScale" parameter here in order to properly
    // handle hairlines. Their with is determined in device space, unlike
    // other strokes.
    return 1;
  }
  // since we're stroked, outset the rect by the radius (and join type, caps)
  float multiplier = 1;
  if (StrokeJoin::kMiter == join) multiplier = std::max(multiplier, miter_limit);
  if (StrokeCap::kSquare == cap) multiplier = std::max(multiplier, kScalarSqrt2);
  return stroke_width / 2 * multiplier;
}

void Stroke::StrokePath(const ScalarPath& src, ScalarPath* dst) const {
  const float radius = width_ * 0.5f;
  if (radius <= 0) return;

  // If src is really a rect, call our specialty strokeRect() method
  {
    ScalarRect rect;
    bool is_closed = false;
    PathDirection dir;
    if (src.IsRect(&rect, &is_closed, &dir) && is_closed) {
      StrokeRect(rect, dst, dir);
      return;
    }
  }

  // Centers can only be ignored when stroking and filling, which is not
  // ported.
  PathStroker stroker(src, radius, miter_limit_, cap_, join_, res_scale_);

  ScalarPath::Iter iter(src, false);
  ScalarPath::Verb last_segment = ScalarPath::Verb::kMove;
  ScalarPoint pts[4];
  while (const auto verb = iter.Next(pts)) {
    switch (*verb) {
    case ScalarPath::Verb::kMove:
      stroker.MoveTo(pts[0]);
      break;
    case ScalarPath::Verb::kLine:
      stroker.LineTo(pts[1], &iter);
      last_segment = ScalarPath::Verb::kLine;
      break;
    case ScalarPath::Verb::kQuad:
      stroker.QuadTo(pts[1], pts[2]);
      last_segment = ScalarPath::Verb::kQuad;
      break;
    case ScalarPath::Verb::kConic:
      stroker.ConicTo(pts[1], pts[2], iter.ConicWeight());
      last_segment = ScalarPath::Verb::kConic;
      break;
    case ScalarPath::Verb::kCubic:
      stroker.CubicTo(pts[1], pts[2], pts[3]);
      last_segment = ScalarPath::Verb::kCubic;
      break;
    case ScalarPath::Verb::kClose:
      if (StrokeCap::kButt != cap_) {
        // If the stroke consists of a moveTo followed by a close, treat it as
        // if it were followed by a zero-length line. Lines without length can
        // have square and round end caps.
        if (stroker.HasOnlyMoveTo()) {
          stroker.LineTo(stroker.MoveToPt());
          last_segment = ScalarPath::Verb::kLine;
          break;
        }
        // If the stroke consists of a moveTo followed by one or more
        // zero-length verbs, then followed by a close, treat is as if it were
        // followed by a zero-length line. Lines without length can have
        // square & round end caps.
        if (stroker.IsCurrentContourEmpty()) {
          last_segment = ScalarPath::Verb::kLine;
          break;
        }
      }
      stroker.Close(last_segment == ScalarPath::Verb::kLine);
      break;
    }
  }
  stroker.Done(dst, last_segment == ScalarPath::Verb::kLine);
}

void Stroke::StrokeRect(const ScalarRect& orig_rect, ScalarPath* dst, PathDirection dir) const {
  dst->Reset();

  const float radius = width_ * 0.5f;
  if (radius <= 0) return;

  float rw = orig_rect.Width();
  float rh = orig_rect.Height();
  if ((rw < 0) ^ (rh < 0)) dir = ReverseDirection(dir);
  ScalarRect rect = orig_rect;
  rect.Sort();
  // reassign these, now that we know they'll be >= 0
  rw = rect.Width();
  rh = rect.Height();

  ScalarRect r = rect;
  r.Outset(radius, radius);

  StrokeJoin join = join_;
  if (StrokeJoin::kMiter == join && miter_limit_ < kScalarSqrt2) join = StrokeJoin::kBevel;

  switch (join) {
  case StrokeJoin::kMiter:
    dst->AddRect(r, dir);
    break;
  case StrokeJoin::kBevel:
    AddBevel(dst, rect, r, dir);
    break;
  case StrokeJoin::kRound:
    dst->AddRRect(r, radius, radius, dir);
    break;
  }

  if (width_ < std::min(rw, rh)) {
    r = rect;
    r.Inset(radius, radius);
    dst->AddRect(r, ReverseDirection(dir));
  }
}

} // namespace bkfont
