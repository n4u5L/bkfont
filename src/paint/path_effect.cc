// Ported from: skia/src/effects/SkDashPathEffect.cpp
// Ported from: skia/src/utils/SkDashPath.cpp
// Ported from: skia/src/core/SkPathUtils.cpp
// Ported from: skia/src/core/SkMatrix.cpp (ComputeResScaleForStroking)

#include "path_effect.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "path_geometry.h"
#include "path_measure.h"
#include "platform_paint.h"

namespace bkit {

namespace {

// SkDashPath::kMaxDashCount.
constexpr float kMaxDashCount = 1000000;

bool IsEven(int x) {
  return !(x & 1);
}

float ScalarMod(float x, float y) {
  return std::fmod(x, y);
}

float FindFirstInterval(std::span<const float> intervals, float phase, std::size_t* index) {
  for (std::size_t i = 0; i < intervals.size(); ++i) {
    const float gap = intervals[i];
    if (phase > gap || (phase == gap && gap)) {
      phase -= gap;
    } else {
      *index = i;
      return gap - phase;
    }
  }
  // If we get here, phase "appears" to be larger than our length. This
  // shouldn't happen with perfect precision, but we can accumulate errors
  // during the initial length computation (rounding can make our sum be too
  // big or too small. In that event, we just have to eat the error here.
  *index = 0;
  return intervals[0];
}

void OutsetForStroke(ScalarRect* rect, const StrokeRec& rec) {
  float radius = rec.GetWidth() * 0.5f;
  if (0 == radius) radius = 1; // hairlines
  if (StrokeJoin::kMiter == rec.GetJoin()) radius *= rec.GetMiter();
  rect->Outset(radius, radius);
}

// If line is zero-length, bump out the end by a tiny amount to draw endcaps.
// The bump factor is sized so that SkPoint::Distance() computes a non-zero
// length. Offsets SK_ScalarNearlyZero or smaller create empty paths when Iter
// measures length. Large values are scaled by SK_ScalarNearlyZero so
// significant bits change.
void AdjustZeroLengthLine(ScalarPoint pts[2]) {
  pts[1].x += std::max(1.001f, pts[1].x) * kScalarNearlyZero;
}

bool ClipLine(ScalarPoint pts[2], const ScalarRect& bounds, float interval_length, float prior_phase) {
  const ScalarPoint dxy = pts[1] - pts[0];
  // only horizontal or vertical lines
  if (dxy.x && dxy.y) return false;
  const bool vertical = dxy.y != 0; // 0 to adjust horizontal, 1 to adjust vertical
  const auto coord = [&](ScalarPoint& p) -> float& {
    return vertical ? p.y : p.x;
  };

  float min_xy = coord(pts[0]);
  float max_xy = coord(pts[1]);
  const bool swapped = max_xy < min_xy;
  if (swapped) std::swap(min_xy, max_xy);

  const float left_top = vertical ? bounds.top : bounds.left;
  const float right_bottom = vertical ? bounds.bottom : bounds.right;
  if (max_xy < left_top || min_xy > right_bottom) return false;

  // Now we actually perform the chop, removing the excess to the left/top and
  // right/bottom of the bounds (keeping our new line "in phase" with the
  // dash, hence the (mod intervalLength).
  if (min_xy < left_top) {
    min_xy = left_top - ScalarMod(left_top - min_xy, interval_length);
    if (!swapped) min_xy -= prior_phase; // for rectangles, adjust by prior phase
  }
  if (max_xy > right_bottom) {
    max_xy = right_bottom + ScalarMod(max_xy - right_bottom, interval_length);
    if (swapped) max_xy += prior_phase; // for rectangles, adjust by prior phase
  }
  if (swapped) std::swap(min_xy, max_xy);
  coord(pts[0]) = min_xy;
  coord(pts[1]) = max_xy;
  if (min_xy == max_xy) AdjustZeroLengthLine(pts);
  return true;
}

// Handles only lines and rects. If cull_path() returns true, dst_path is the
// new smaller path, otherwise dst_path may have been changed but you should
// ignore it.
bool CullPath(const ScalarPath& src_path, const StrokeRec& rec, const ScalarRect* cull_rect, float interval_length,
              ScalarPath* dst_path) {
  if (!cull_rect) {
    ScalarPoint pts[2];
    if (src_path.IsLine(pts) && pts[0] == pts[1]) {
      AdjustZeroLengthLine(pts);
      dst_path->MoveTo(pts[0]);
      dst_path->LineTo(pts[1]);
      return true;
    }
    return false;
  }

  ScalarRect bounds = *cull_rect;
  OutsetForStroke(&bounds, rec);

  {
    ScalarPoint pts[2];
    if (src_path.IsLine(pts)) {
      if (ClipLine(pts, bounds, interval_length, 0)) {
        dst_path->MoveTo(pts[0]);
        dst_path->LineTo(pts[1]);
        return true;
      }
      return false;
    }
  }

  if (src_path.IsRect(nullptr)) {
    // We'll break the rect into four lines, culling each separately.
    ScalarPath::Iter iter(src_path, false);
    ScalarPoint it_pts[4];
    (void)iter.Next(it_pts); // the move
    double accum = 0; // Sum of unculled edge lengths to keep the phase correct.
                      // Intentionally a double to minimize the risk of overflow and drift.
    std::optional<ScalarPath::Verb> verb;
    while ((verb = iter.Next(it_pts)) && *verb == ScalarPath::Verb::kLine) {
      // Notice this vector v and accum work with the original unclipped length.
      const ScalarPoint v = it_pts[1] - it_pts[0];
      ScalarPoint pts[2] = {it_pts[0], it_pts[1]};
      if (ClipLine(pts, bounds, interval_length, static_cast<float>(std::fmod(accum, interval_length)))) {
        // pts[0] may have just been changed by clip_line(). If that's not
        // where we ended the previous lineTo(), we need to moveTo() there.
        const std::optional<ScalarPoint> last = dst_path->GetLastPt();
        if (!last || !(*last == pts[0])) dst_path->MoveTo(pts[0]);
        dst_path->LineTo(pts[1]);
      }
      // We either just traveled v.fX horizontally or v.fY vertically.
      accum += std::fabs(v.x + v.y);
    }
    return !dst_path->IsEmpty();
  }

  return false;
}

class SpecialLineRec {
public:
  bool Init(const ScalarPath& src, StrokeRec* rec, int interval_count, float interval_length) {
    if (rec->IsHairlineStyle() || !src.IsLine(pts_)) return false;

    // can relax this in the future, if we handle square and round caps
    if (StrokeCap::kButt != rec->GetCap()) return false;

    const float path_length = point::Distance(pts_[0], pts_[1]);

    tangent_ = pts_[1] - pts_[0];
    if (tangent_.IsZero()) return false;

    path_length_ = path_length;
    tangent_ *= 1.0f / path_length;
    if (!std::isfinite(tangent_.x) || !std::isfinite(tangent_.y)) return false;
    point::RotateCCW(tangent_, &normal_);
    normal_ *= rec->GetWidth() * 0.5f;

    // now estimate how many quads will be added to the path
    //     resulting segments = pathLen * intervalCount / intervalLen
    //     resulting points = 4 * segments
    float pt_count = path_length * static_cast<float>(interval_count) / interval_length;
    pt_count = std::min(pt_count, kMaxDashCount);
    if (std::isnan(pt_count)) return false;

    // we will take care of the stroking
    rec->SetFillStyle();
    return true;
  }

  void AddSegment(float d0, float d1, ScalarPath* path) const {
    // clamp the segment to our length
    if (d1 > path_length_) d1 = path_length_;

    const float x0 = pts_[0].x + tangent_.x * d0;
    const float x1 = pts_[0].x + tangent_.x * d1;
    const float y0 = pts_[0].y + tangent_.y * d0;
    const float y1 = pts_[0].y + tangent_.y * d1;

    ScalarPoint pts[4];
    pts[0] = {x0 + normal_.x, y0 + normal_.y}; // moveTo
    pts[1] = {x1 + normal_.x, y1 + normal_.y}; // lineTo
    pts[2] = {x1 - normal_.x, y1 - normal_.y}; // lineTo
    pts[3] = {x0 - normal_.x, y0 - normal_.y}; // lineTo

    path->AddPolygon(pts, false);
  }

private:
  ScalarPoint pts_[2];
  ScalarPoint tangent_;
  ScalarPoint normal_;
  float path_length_ = 0;
};

} // namespace

namespace dash_path {

void CalcDashParameters(float phase, std::span<const float> intervals, float* initial_dash_length,
                        std::size_t* initial_dash_index, float* interval_length, float* adjusted_phase) {
  float len = 0;
  for (const float interval : intervals) len += interval;
  *interval_length = len;
  // Adjust phase to be between 0 and len, "flipping" phase if negative.
  // e.g., if len is 100, then phase of -20 (or -120) is equivalent to 80
  if (adjusted_phase) {
    if (phase < 0) {
      phase = -phase;
      if (phase > len) phase = ScalarMod(phase, len);
      phase = len - phase;

      // Due to finite precision, it's possible that phase == len, even after
      // the subtract (if len >>> phase), so fix that here. This fixes
      // http://crbug.com/124652 .
      if (phase == len) phase = 0;
    } else if (phase >= len) {
      phase = ScalarMod(phase, len);
    }
    *adjusted_phase = phase;
  }
  *initial_dash_length = FindFirstInterval(intervals, phase, initial_dash_index);
}

bool InternalFilter(ScalarPath* dst, const ScalarPath& src, StrokeRec* rec, const ScalarRect* cull_rect,
                    std::span<const float> a_intervals, float initial_dash_length, int initial_dash_index,
                    float interval_length, float start_phase) {
  const std::size_t count = a_intervals.size();

  // we do nothing if the src wants to be filled
  const StrokeRec::Style style = rec->GetStyle();
  if (StrokeRec::kFill_Style == style) return false;

  const float* intervals = a_intervals.data();
  float dash_count = 0;

  ScalarPath cull_path_storage;
  const ScalarPath* src_ptr = &src;
  if (CullPath(src, *rec, cull_rect, interval_length, &cull_path_storage)) {
    // if rect is closed, starts in a dash, and ends in a dash, add the initial
    // join potentially a better fix is described here: skbug.com/40038693
    if (src.IsRect(nullptr) && src.IsLastContourClosed() && IsEven(initial_dash_index)) {
      const float path_length = PathMeasure(src, false, rec->GetResScale()).GetLength();
      float end_phase = ScalarMod(path_length + start_phase, interval_length);
      std::size_t index = 0;
      while (end_phase > intervals[index]) {
        end_phase -= intervals[index++];
        if (index == count) {
          // We have run out of intervals. endPhase "should" never get to this
          // point, but it could if the subtracts underflowed. Hence we will
          // pin it as if it perfectly ran through the intervals. See
          // crbug.com/875494 (and skbug.com/40039544)
          end_phase = 0;
          break;
        }
      }
      // if dash ends inside "on", or ends at beginning of "off"
      if (IsEven(static_cast<int>(index)) == (end_phase > 0)) {
        const auto points = src.Points();
        const ScalarPoint mid_point = points[0];
        // get vector at end of rect
        int last = src.CountPoints() - 1;
        while (mid_point == points[static_cast<std::size_t>(last)]) --last;
        // get vector at start of rect
        int next = 1;
        while (mid_point == points[static_cast<std::size_t>(next)]) ++next;
        ScalarPoint v = mid_point - points[static_cast<std::size_t>(last)];
        const float kTinyOffset = kScalarNearlyZero;
        // scale vector to make start of tiny right angle
        v *= kTinyOffset;
        cull_path_storage.MoveTo(mid_point - v);
        cull_path_storage.LineTo(mid_point);
        v = mid_point - points[static_cast<std::size_t>(next)];
        // scale vector to make end of tiny right angle
        v *= kTinyOffset;
        cull_path_storage.LineTo(mid_point - v);
      }
    }
    src_ptr = &cull_path_storage;
  }

  SpecialLineRec line_rec;
  const bool special_line = line_rec.Init(*src_ptr, rec, static_cast<int>(count >> 1), interval_length);

  PathMeasure meas(*src_ptr, false, rec->GetResScale());

  do {
    bool skip_first_segment = meas.IsClosed();
    bool added_segment = false;
    const float length = meas.GetLength();
    std::size_t index = static_cast<std::size_t>(initial_dash_index);

    // Since the path length / dash length ratio may be arbitrarily large, we
    // can exert significant memory pressure while attempting to build the
    // filtered path. To avoid this, we simply give up dashing beyond a
    // certain threshold.
    //
    // The original bug report (http://crbug.com/165432) is based on a path
    // yielding more than 90 million dash segments and crashing the memory
    // allocator. A limit of 1 million segments seems reasonable: at 2 verbs
    // per segment * 9 bytes per verb, this caps the maximum dash memory
    // overhead at roughly 17MB per path.
    dash_count += length * static_cast<float>(count >> 1) / interval_length;
    if (dash_count > kMaxDashCount) {
      dst->Reset();
      return false;
    }

    // Using double precision to avoid looping indefinitely due to single
    // precision rounding (for extreme path_length/dash_length ratios). See
    // test_infinite_dash() unittest.
    double distance = 0;
    double dlen = initial_dash_length;

    while (distance < length) {
      added_segment = false;
      if (IsEven(static_cast<int>(index)) && !skip_first_segment) {
        added_segment = true;
        if (special_line) {
          line_rec.AddSegment(static_cast<float>(distance), static_cast<float>(distance + dlen), dst);
        } else {
          meas.GetSegment(static_cast<float>(distance), static_cast<float>(distance + dlen), dst, true);
        }
      }
      distance += dlen;

      // clear this so we only respect it the first time around
      skip_first_segment = false;

      // wrap around our intervals array if necessary
      index += 1;
      if (index == count) index = 0;

      // fetch our next dlen
      dlen = intervals[index];
    }

    // extend if we ended on a segment and we need to join up with the
    // (skipped) initial segment
    if (meas.IsClosed() && IsEven(initial_dash_index) && initial_dash_length >= 0) {
      meas.GetSegment(0, initial_dash_length, dst, !added_segment);
    }
  } while (meas.NextContour());

  return true;
}

bool ValidDashPath(float phase, std::span<const float> intervals) {
  if (intervals.size() < 2 || (intervals.size() & 1)) return false;
  float length = 0;
  for (const float interval : intervals) {
    if (interval < 0) return false;
    length += interval;
  }
  // watch out for values that might make us go out of bounds
  return length > 0 && std::isfinite(phase) && std::isfinite(length);
}

} // namespace dash_path

std::shared_ptr<const PathEffect> DashPathEffect::Make(std::span<const float> intervals, float phase) {
  if (!dash_path::ValidDashPath(phase, intervals)) return nullptr;
  return std::make_shared<const DashPathEffect>(intervals, phase);
}

DashPathEffect::DashPathEffect(std::span<const float> intervals, float phase)
    : intervals_(intervals.begin(), intervals.end()),
      phase_(0),
      initial_dash_length_(-1),
      interval_length_(0),
      initial_dash_index_(0) {
  // set the internal data members
  std::size_t initial_dash_index = 0;
  dash_path::CalcDashParameters(phase, intervals_, &initial_dash_length_, &initial_dash_index, &interval_length_,
                                &phase_);
  initial_dash_index_ = static_cast<int>(initial_dash_index);
}

bool DashPathEffect::FilterPath(ScalarPath* dst, const ScalarPath& src, StrokeRec* rec, const ScalarRect* cull_rect,
                                const ScalarMatrix&) const {
  return dash_path::InternalFilter(dst, src, rec, cull_rect, intervals_, initial_dash_length_, initial_dash_index_,
                                   interval_length_, phase_);
}

bool DashPathEffect::Equals(const PathEffect& other) const {
  const auto* dash = dynamic_cast<const DashPathEffect*>(&other);
  return dash && dash->intervals_ == intervals_ && dash->phase_ == phase_;
}

float ComputeResScaleForStroking(const ScalarMatrix& matrix) {
  // Not sure how to handle perspective differently, so we just don't try
  // (yet)
  const float sx = point::Length(matrix.GetScaleX(), matrix.GetSkewY());
  const float sy = point::Length(matrix.GetSkewX(), matrix.GetScaleY());
  if (std::isfinite(sx) && std::isfinite(sy)) {
    const float scale = std::max(sx, sy);
    if (scale > 0) return scale;
  }
  return 1;
}

bool FillPathWithPaint(const ScalarPath& orig_src, const PlatformPaint& paint, ScalarPath* dst,
                       const ScalarRect* cull_rect, const ScalarMatrix& ctm) {
  dst->Reset();
  if (!orig_src.IsFinite()) return false;

  const float res_scale = ComputeResScaleForStroking(ctm);
  StrokeRec rec(paint, res_scale);

  const ScalarPath* src_ptr = &orig_src;
  ScalarPath path_storage;
  const PathEffect* pe = paint.GetPathEffect().get();
  if (pe && pe->FilterPath(dst, orig_src, &rec, cull_rect, ctm)) {
    path_storage = std::move(*dst);
    dst->Reset();
    src_ptr = &path_storage;
  }
  if (!rec.ApplyToPath(dst, *src_ptr)) *dst = *src_ptr;
  if (!dst->IsFinite()) dst->Reset();
  return !rec.IsHairlineStyle();
}

} // namespace bkit
