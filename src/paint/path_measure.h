// Ported from: skia/include/core/SkContourMeasure.h
// Ported from: skia/src/core/SkContourMeasure.cpp
// Ported from: skia/include/core/SkPathMeasure.h
// Ported from: skia/src/core/SkPathMeasure.cpp

#pragma once

#include <memory>
#include <vector>

#include "path.h"

namespace bkit {

// SkContourMeasure: the length of one contour, and its pieces.
class ContourMeasure {
public:
  float Length() const {
    return length_;
  }
  bool IsClosed() const {
    return is_closed_;
  }

  // Pins distance to 0 <= distance <= Length(), and then computes the
  // corresponding position and tangent.
  bool GetPosTan(float distance, ScalarPoint* position, ScalarPoint* tangent) const;

  // Given a start and stop distance, return in dst the intervening
  // segment(s). If the segment is zero-length, return false, else return
  // true. startD and stopD are pinned to legal values (0..Length()). If
  // startD > stopD then return false (and leave dst untouched). Begin the
  // segment with a moveTo if `start_with_move_to` is true.
  bool GetSegment(float start_d, float stop_d, ScalarPath* dst, bool start_with_move_to) const;

  // SkContourMeasure::Segment.
  struct Segment {
    float distance;     // total distance up to this point
    unsigned pt_index;  // index into the pts array
    unsigned t_value : 30;
    unsigned type : 2;  // actually the enum SkSegType

    float GetScalarT() const;
  };

  ContourMeasure(std::vector<Segment> segments, std::vector<ScalarPoint> pts, float length, bool is_closed);

private:
  const Segment* DistanceToSegment(float distance, float* t) const;

  std::vector<Segment> segments_;
  std::vector<ScalarPoint> pts_; // Points used to define the segments
  float length_;
  bool is_closed_;
};

// SkContourMeasureIter.
class ContourMeasureIter {
public:
  // Initialize the Iter with a path. The parts of the path that are needed
  // are copied, so the client is free to modify/delete the path after this
  // call.
  //
  // `res_scale` controls the precision of the measure. values > 1 increase
  // the precision (and possibly slow down the computation).
  ContourMeasureIter(const ScalarPath& path, bool force_closed, float res_scale = 1);
  ~ContourMeasureIter();

  // Iterates through contours in path, returning a contour-measure object
  // for each contour in the path. Returns null when it is done.
  std::unique_ptr<ContourMeasure> Next();

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

// SkPathMeasure.
class PathMeasure {
public:
  PathMeasure(const ScalarPath& path, bool force_closed, float res_scale = 1);
  ~PathMeasure();

  // Return the total length of the current contour, or 0 if no path is
  // associated.
  float GetLength() const;
  bool GetSegment(float start_d, float stop_d, ScalarPath* dst, bool start_with_move_to) const;
  // Return true if the current contour is closed().
  bool IsClosed() const;
  // Move to the next contour in the path. Return true if one exists, or
  // false if we're done with the path.
  bool NextContour();

private:
  ContourMeasureIter iter_;
  std::unique_ptr<ContourMeasure> contour_;
};

} // namespace bkit
