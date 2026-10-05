// Ported from: skia/include/core/SkStrokeRec.h
// Ported from: skia/src/core/SkStrokeRec.cpp
// Ported from: skia/src/core/SkStroke.h
// Ported from: skia/src/core/SkStroke.cpp
// Ported from: skia/src/core/SkStrokerPriv.cpp

#pragma once

#include <cstdint>

#include "path.h"

namespace bkfont {

// SkPaint::Cap.
enum class StrokeCap : std::uint8_t {
  kButt,
  kRound,
  kSquare,
};

// SkPaint::Join.
enum class StrokeJoin : std::uint8_t {
  kMiter,
  kRound,
  kBevel,
};

// SkPaintDefaults_MiterLimit.
inline constexpr float kDefaultMiterLimit = 4;

class PlatformPaint;

// SkStrokeRec. PlatformPaint has no stroke-and-fill style, so neither does
// the record.
class StrokeRec {
public:
  enum InitStyle {
    kHairline_InitStyle,
    kFill_InitStyle,
  };
  enum Style {
    kHairline_Style,
    kFill_Style,
    kStroke_Style,
  };

  explicit StrokeRec(InitStyle);
  StrokeRec(const PlatformPaint&, float res_scale = 1);

  Style GetStyle() const;
  float GetWidth() const {
    return width_;
  }
  float GetMiter() const {
    return miter_limit_;
  }
  StrokeCap GetCap() const {
    return cap_;
  }
  StrokeJoin GetJoin() const {
    return join_;
  }
  float GetResScale() const {
    return res_scale_;
  }
  bool IsHairlineStyle() const {
    return kHairline_Style == GetStyle();
  }
  bool IsFillStyle() const {
    return kFill_Style == GetStyle();
  }

  void SetFillStyle();
  void SetHairlineStyle();
  void SetStrokeStyle(float width);
  void SetStrokeParams(StrokeCap cap, StrokeJoin join, float miter_limit) {
    cap_ = cap;
    join_ = join;
    miter_limit_ = miter_limit;
  }
  void SetResScale(float res_scale) {
    res_scale_ = res_scale;
  }

  // Applies this stroke to `src`, storing the result in `dst`. Returns false
  // for a hairline or fill, leaving `dst` unchanged.
  bool ApplyToPath(ScalarPath* dst, const ScalarPath& src) const;

  // How much a stroked path's bounds outset its geometry's bounds.
  float GetInflationRadius() const;
  static float GetInflationRadius(StrokeJoin, float miter_limit, StrokeCap, float stroke_width);

private:
  float res_scale_;
  float width_;
  float miter_limit_;
  StrokeCap cap_;
  StrokeJoin join_;
};

// SkStroke.
class Stroke {
public:
  Stroke() = default;

  void SetWidth(float width) {
    width_ = width;
  }
  void SetMiterLimit(float miter_limit) {
    miter_limit_ = miter_limit;
  }
  void SetCap(StrokeCap cap) {
    cap_ = cap;
  }
  void SetJoin(StrokeJoin join) {
    join_ = join;
  }
  // The resolution scale lets the stroker add fewer curve segments for
  // smaller output.
  void SetResScale(float res_scale) {
    res_scale_ = res_scale;
  }

  // Strokes `src` into `dst`, which is replaced.
  void StrokePath(const ScalarPath& src, ScalarPath* dst) const;

private:
  void StrokeRect(const ScalarRect& rect, ScalarPath* dst, PathDirection) const;

  float width_ = 1;
  float miter_limit_ = kDefaultMiterLimit;
  float res_scale_ = 1;
  StrokeCap cap_ = StrokeCap::kButt;
  StrokeJoin join_ = StrokeJoin::kMiter;
};

} // namespace bkfont
