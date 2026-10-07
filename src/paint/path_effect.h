// Ported from: skia/include/core/SkPathEffect.h
// Ported from: skia/src/core/SkPathEffectBase.h
// Ported from: skia/include/effects/SkDashPathEffect.h
// Ported from: skia/src/effects/SkDashImpl.h
// Ported from: skia/src/effects/SkDashPathEffect.cpp
// Ported from: skia/src/utils/SkDashPath.cpp
// Ported from: skia/src/core/SkPathUtils.cpp
// Ported from: cc/paint/path_effect.h

#pragma once

#include <memory>
#include <span>
#include <vector>

#include "path.h"
#include "stroke.h"

namespace bkit {

class PlatformPaint;

// SkPathEffect. The only effect is the dash effect.
class PathEffect {
public:
  virtual ~PathEffect() = default;

  // Given a src path (input) and a stroke-rec (input and output), apply this
  // effect to the src path, returning the new path in dst, and return true.
  // If this effect cannot be applied, return false and ignore dst and
  // stroke-rec.
  virtual bool FilterPath(ScalarPath* dst, const ScalarPath& src, StrokeRec* rec, const ScalarRect* cull_rect,
                          const ScalarMatrix& ctm) const = 0;

  virtual bool Equals(const PathEffect& other) const = 0;
};

// SkDashImpl.
class DashPathEffect final : public PathEffect {
public:
  // SkDashPathEffect::Make (and cc::PathEffect::MakeDash): null for invalid
  // intervals.
  static std::shared_ptr<const PathEffect> Make(std::span<const float> intervals, float phase);

  DashPathEffect(std::span<const float> intervals, float phase);

  bool FilterPath(ScalarPath* dst, const ScalarPath& src, StrokeRec* rec, const ScalarRect* cull_rect,
                  const ScalarMatrix& ctm) const override;
  bool Equals(const PathEffect& other) const override;

private:
  std::vector<float> intervals_;
  float phase_;
  float initial_dash_length_;
  float interval_length_;
  int initial_dash_index_;
};

// SkDashPath.
namespace dash_path {

// Calculates the initialDashLength, initialDashIndex, and intervalLength
// based on the inputed phase and intervals. If adjusted_phase is passed in,
// then the phase will be adjusted to be between 0 and intervalLength. The
// result will be stored in adjusted_phase. If adjusted_phase is nullptr then
// it is assumed phase is already between 0 and intervalLength
void CalcDashParameters(float phase, std::span<const float> intervals, float* initial_dash_length,
                        std::size_t* initial_dash_index, float* interval_length, float* adjusted_phase = nullptr);

// Caller should have invoked ValidDashPath before calling this.
bool InternalFilter(ScalarPath* dst, const ScalarPath& src, StrokeRec* rec, const ScalarRect* cull_rect,
                    std::span<const float> intervals, float initial_dash_length, int initial_dash_index,
                    float interval_length, float start_phase);

bool ValidDashPath(float phase, std::span<const float> intervals);

} // namespace dash_path

// skpathutils::FillPathWithPaint: applies the paint's path effect and stroke
// to `src`, storing the fill path in `dst`. Returns true if the result is to
// be filled, false if it is a hairline.
bool FillPathWithPaint(const ScalarPath& src, const PlatformPaint& paint, ScalarPath* dst,
                       const ScalarRect* cull_rect, const ScalarMatrix& ctm);

// SkMatrixPriv::ComputeResScaleForStroking.
float ComputeResScaleForStroking(const ScalarMatrix& matrix);

} // namespace bkit
