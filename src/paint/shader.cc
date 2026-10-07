// Ported from: skia/src/shaders/gradients/SkGradientBaseShader.cpp
// Ported from: skia/src/shaders/gradients/SkLinearGradient.cpp
// Ported from: skia/src/shaders/gradients/SkRadialGradient.cpp
// Ported from: skia/src/shaders/gradients/SkConicalGradient.cpp
// Ported from: skia/src/shaders/gradients/SkSweepGradient.cpp
// Ported from: skia/src/shaders/SkImageShader.cpp
// Ported from: skia/src/shaders/SkShaderBase.cpp
// Ported from: skia/src/shaders/SkLocalMatrixShader.cpp
// Ported from: skia/src/core/SkMipmapAccessor.cpp
// Ported from: skia/src/core/SkPoint.cpp
// Ported from: skia/src/opts/SkRasterPipeline_opts.h

#include "shader.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

#include "scalar.h"

namespace bkit {

namespace {

// SkGradientBaseShader::kDegenerateThreshold.
constexpr float kDegenerateThreshold = 1.0f / (1 << 15);

bool NearlyZero(float x, float tolerance = kScalarNearlyZero) {
  return std::abs(x) <= tolerance;
}

bool NearlyEqual(float x, float y, float tolerance = kScalarNearlyZero) {
  return std::abs(x - y) <= tolerance;
}

float Length(float dx, float dy) {
  const float mag2 = dx * dx + dy * dy;
  if (std::isfinite(mag2)) {
    return std::sqrt(mag2);
  }
  // SkPoint::Length retries in double when the float squares overflow.
  const double x = dx;
  const double y = dy;
  return static_cast<float>(std::sqrt(x * x + y * y));
}

ScalarMatrix LinearPointsToUnit(const ScalarPoint pts[2]) {
  const float dx = pts[1].x - pts[0].x;
  const float dy = pts[1].y - pts[0].y;
  const float magnitude = Length(dx, dy);
  const float inverse = magnitude ? 1 / magnitude : 0;
  ScalarMatrix matrix;
  matrix.SetSinCos(-dy * inverse, dx * inverse, pts[0].x, pts[0].y);
  matrix.PostTranslate(-pts[0].x, -pts[0].y);
  matrix.PostScale(inverse, inverse);
  return matrix;
}

ScalarMatrix RadialToUnit(const ScalarPoint& center, float radius) {
  ScalarMatrix matrix = ScalarMatrix::Translate(-center.x, -center.y);
  matrix.PostScale(1 / radius, 1 / radius);
  return matrix;
}

// SkMatrix::PolyToPoly for two source points and destination {(0,0),(1,0)}.
bool MapToUnitX(const ScalarPoint& start, const ScalarPoint& end, ScalarMatrix* matrix) {
  const float dx = end.x - start.x;
  const float dy = end.y - start.y;
  const ScalarMatrix source = ScalarMatrix::MakeAll(dy, dx, start.x, -dx, dy, start.y);
  ScalarMatrix inverse;
  if (!source.Invert(&inverse)) {
    return false;
  }
  matrix->SetConcat(ScalarMatrix::MakeAll(0, 1, -1, 0), inverse);
  return true;
}

// clamp_gamut for 8888 destinations: alpha in [0, 1] and color in [0, a].
PMColor4f ClampGamut(const PMColor4f& c) {
  const float a = std::clamp(c.a, 0.0f, 1.0f);
  return {std::clamp(c.r, 0.0f, a), std::clamp(c.g, 0.0f, a), std::clamp(c.b, 0.0f, a), a};
}

class EmptyShader final : public Shader {
public:
  std::unique_ptr<Context> MakeContext(const ScalarMatrix&, const Color4f&) const override {
    return nullptr;
  }
};

class ColorShader final : public Shader {
public:
  explicit ColorShader(const Color4f& color)
      : color_(color) {
  }

  std::unique_ptr<Context> MakeContext(const ScalarMatrix&, const Color4f& paint_color) const override {
    class ColorContext final : public Context {
    public:
      explicit ColorContext(const PMColor4f& color)
          : color_(color) {
      }
      void ShadeSpan(int, int, int count, PMColor4f* out) override {
        std::fill(out, out + count, color_);
      }

    private:
      PMColor4f color_;
    };
    // The paint alpha modulates the shader color.
    Color4f color = color_;
    color.a *= paint_color.a;
    return std::make_unique<ColorContext>(ClampGamut(color.Premul()));
  }

  bool IsOpaque() const override {
    return color_.IsOpaque();
  }

  bool AsLuminanceColor(Color4f* color) const override {
    if (color) {
      *color = {color_.r, color_.g, color_.b, 1};
    }
    return true;
  }

private:
  Color4f color_;
};

// The colors and positions of a gradient after SkGradientBaseShader's
// constructor: bracketed by [0, 1], pinned, monotonic and deduplicated. The
// positions are empty when the stops are uniform.
struct GradientStops {
  std::vector<Color4f> colors;
  std::vector<float> positions;
  TileMode tile_mode = TileMode::kClamp;
  bool colors_are_opaque = true;
};

GradientStops NormalizeStops(std::span<const Color4f> in_colors, const float* in_positions, TileMode tile_mode) {
  GradientStops stops;
  stops.tile_mode = tile_mode;
  const int desc_count = static_cast<int>(in_colors.size());

  // Note: we let the caller skip the first and/or last position.
  // i.e. pos[0] = 0.3, pos[1] = 0.7
  // In these cases, we insert entries to ensure that the final data will be
  // bracketed by [0, 1].
  bool first_stop_is_implicit = false;
  bool last_stop_is_implicit = false;
  int color_count = desc_count;
  if (in_positions) {
    first_stop_is_implicit = in_positions[0] > 0;
    last_stop_is_implicit = in_positions[desc_count - 1] != 1;
    color_count += first_stop_is_implicit + last_stop_is_implicit;
  }

  // Now copy over the colors, adding the duplicates at t=0 and t=1 as needed
  if (first_stop_is_implicit) {
    stops.colors.push_back(in_colors[0]);
  }
  for (int i = 0; i < desc_count; ++i) {
    stops.colors.push_back(in_colors[static_cast<std::size_t>(i)]);
    stops.colors_are_opaque = stops.colors_are_opaque && (in_colors[static_cast<std::size_t>(i)].a == 1);
  }
  if (last_stop_is_implicit) {
    stops.colors.push_back(in_colors[static_cast<std::size_t>(desc_count - 1)]);
  }

  if (in_positions) {
    float prev = 0;
    stops.positions.push_back(prev); // force the first pos to 0

    int start_index = first_stop_is_implicit ? 0 : 1;
    int count = desc_count + last_stop_is_implicit;

    bool uniform_stops = true;
    const float uniform_step = in_positions[start_index] - prev;
    for (int i = start_index; i < count; i++) {
      // Pin the last value to 1.0, and make sure pos is monotonic.
      float curr = 1.0f;
      if (i != desc_count) {
        curr = std::clamp(in_positions[i], prev, 1.0f);

        // If a value is clamped to 1.0 before the last stop, the last stop
        // actually isn't implicit if we thought it was.
        if (curr == 1.0f && last_stop_is_implicit) {
          last_stop_is_implicit = false;
        }
      }

      uniform_stops &= NearlyEqual(uniform_step, curr - prev);

      stops.positions.push_back(prev = curr);
    }

    if (uniform_stops) {
      // If the stops are uniform, treat them as implicit.
      stops.positions.clear();
    } else {
      // Remove duplicate stops with more than two of the same stop, keeping
      // the leftmost and rightmost stop colors.
      // i.e.       0, 0, 0,   0.2, 0.2, 0.3, 0.3, 0.3, 1, 1
      // w/  clamp  0,    0,   0.2, 0.2, 0.3,      0.3, 1, 1
      // w/o clamp        0,   0.2, 0.2, 0.3,      0.3, 1
      std::vector<float>& positions = stops.positions;
      std::vector<Color4f>& colors = stops.colors;
      int i = 0;
      int deduped_color_count = 0;
      for (int j = 1; j <= color_count; j++) {
        // We can compare the current positions at i and j since once these
        // positions are overwritten, our i and j pointers will be past the
        // overwritten values.
        if (j == color_count || positions[static_cast<std::size_t>(i)] != positions[static_cast<std::size_t>(j)]) {
          bool dup_stop = j - i > 1;

          // Ignore the leftmost stop (i) if it is a non-clamp tilemode with a
          // duplicate stop on t = 0.
          bool ignore_leftmost = dup_stop && tile_mode != TileMode::kClamp && positions[static_cast<std::size_t>(i)] == 0;
          if (!ignore_leftmost) {
            positions[static_cast<std::size_t>(deduped_color_count)] = positions[static_cast<std::size_t>(i)];
            colors[static_cast<std::size_t>(deduped_color_count)] = colors[static_cast<std::size_t>(i)];
            deduped_color_count++;
          }

          // Include the rightmost stop (j-1) only if the stop has a
          // duplicate, ignoring the rightmost stop if it is a non-clamp
          // tilemode with t = 1.
          bool ignore_rightmost = tile_mode != TileMode::kClamp && positions[static_cast<std::size_t>(j - 1)] == 1;
          if (dup_stop && !ignore_rightmost) {
            positions[static_cast<std::size_t>(deduped_color_count)] = positions[static_cast<std::size_t>(j - 1)];
            colors[static_cast<std::size_t>(deduped_color_count)] = colors[static_cast<std::size_t>(j - 1)];
            deduped_color_count++;
          }
          i = j;
        }
      }
      positions.resize(static_cast<std::size_t>(deduped_color_count));
      colors.resize(static_cast<std::size_t>(deduped_color_count));
    }
  }
  return stops;
}

// The gradient fill stages: evenly_spaced_2_stop_gradient,
// evenly_spaced_gradient and gradient. Each stop is color = factor * t + bias.
class GradientLookup {
public:
  explicit GradientLookup(const GradientStops& stops) {
    const std::vector<Color4f>& colors = stops.colors;
    const int count = static_cast<int>(colors.size());
    if (stops.positions.empty()) {
      evenly_spaced_ = true;
      stop_count_ = count;
      const float gap_count = static_cast<float>(count - 1);
      for (int i = 0; i + 1 < count; i++) {
        // factor = (right - left) / gap, and gap = 1/gapCount.
        const Color4f& left = colors[static_cast<std::size_t>(i)];
        const Color4f& right = colors[static_cast<std::size_t>(i + 1)];
        Color4f factor = {(right.r - left.r) * gap_count, (right.g - left.g) * gap_count,
                          (right.b - left.b) * gap_count, (right.a - left.a) * gap_count};
        const float t = static_cast<float>(i) / gap_count;
        factors_.push_back(factor);
        biases_.push_back({left.r - factor.r * t, left.g - factor.g * t, left.b - factor.b * t, left.a - factor.a * t});
      }
      factors_.push_back({0, 0, 0, 0});
      biases_.push_back(colors.back());
      return;
    }

    const std::vector<float>& positions = stops.positions;
    // Remove the default stops inserted by the constructor because they are
    // naturally handled by the search method.
    int first_stop;
    int last_stop;
    if (count > 2) {
      first_stop = colors[0] != colors[1] ? 0 : 1;
      last_stop = colors[static_cast<std::size_t>(count - 2)] != colors[static_cast<std::size_t>(count - 1)] ? count - 1 : count - 2;
    } else {
      first_stop = 0;
      last_stop = 1;
    }

    float t_l = positions[static_cast<std::size_t>(first_stop)];
    Color4f c_l = colors[static_cast<std::size_t>(first_stop)];
    // Stop 0 is the color to use before the first stop; its t is unused.
    AddStop(0, {0, 0, 0, 0}, c_l);

    for (int i = first_stop; i < last_stop; i++) {
      float t_r = positions[static_cast<std::size_t>(i + 1)];
      Color4f c_r = colors[static_cast<std::size_t>(i + 1)];
      if (t_l < t_r) {
        float c_scale = 1 / (t_r - t_l);
        if (std::isfinite(c_scale)) {
          // init_stop_pos.
          Color4f factor = {(c_r.r - c_l.r) * c_scale, (c_r.g - c_l.g) * c_scale,
                            (c_r.b - c_l.b) * c_scale, (c_r.a - c_l.a) * c_scale};
          AddStop(t_l, factor, {c_l.r - factor.r * t_l, c_l.g - factor.g * t_l, c_l.b - factor.b * t_l, c_l.a - factor.a * t_l});
        }
      }
      t_l = t_r;
      c_l = c_r;
    }

    AddStop(t_l, {0, 0, 0, 0}, c_l);
    stop_count_ = static_cast<int>(factors_.size());
  }

  Color4f Lookup(float t) const {
    std::size_t idx = 0;
    if (evenly_spaced_) {
      const float scaled = std::trunc(t * static_cast<float>(stop_count_ - 1));
      idx = static_cast<std::size_t>(std::clamp(scaled, 0.0f, static_cast<float>(stop_count_ - 1)));
    } else {
      // The loop starts at 1 because idx 0 is the color to use before the
      // first stop.
      for (int i = 1; i < stop_count_; i++) {
        idx += t >= ts_[static_cast<std::size_t>(i)] ? 1 : 0;
      }
    }
    const Color4f& f = factors_[idx];
    const Color4f& b = biases_[idx];
    return {f.r * t + b.r, f.g * t + b.g, f.b * t + b.b, f.a * t + b.a};
  }

private:
  void AddStop(float t, const Color4f& factor, const Color4f& bias) {
    ts_.push_back(t);
    factors_.push_back(factor);
    biases_.push_back(bias);
  }

  bool evenly_spaced_ = false;
  int stop_count_ = 0;
  std::vector<float> ts_;
  std::vector<Color4f> factors_;
  std::vector<Color4f> biases_;
};

// The geometry part of a gradient: maps a point in the shader's space to t,
// or reports that the point is masked out (two-point conical degenerates).
class GradientShaderBase : public Shader {
public:
  GradientShaderBase(GradientStops stops, const ScalarMatrix& points_to_unit)
      : stops_(std::move(stops)), lookup_(stops_), points_to_unit_(points_to_unit) {
  }

  std::unique_ptr<Context> MakeContext(const ScalarMatrix& ctm, const Color4f& paint_color) const override {
    ScalarMatrix inverse;
    if (!ctm.Invert(&inverse)) {
      return nullptr;
    }
    inverse.PostConcat(points_to_unit_);

    class GradientContext final : public Context {
    public:
      GradientContext(const GradientShaderBase& shader, const ScalarMatrix& inverse, float paint_alpha)
          : shader_(shader), inverse_(inverse), paint_alpha_(paint_alpha) {
      }

      void ShadeSpan(int x, int y, int count, PMColor4f* out) override {
        for (int i = 0; i < count; ++i) {
          const ScalarPoint p = inverse_.MapPoint({static_cast<float>(x + i) + 0.5f, static_cast<float>(y) + 0.5f});
          out[i] = shader_.ShadePoint(p, paint_alpha_);
        }
      }

    private:
      const GradientShaderBase& shader_;
      ScalarMatrix inverse_;
      float paint_alpha_;
    };
    return std::make_unique<GradientContext>(*this, inverse, paint_color.a);
  }

  bool IsOpaque() const override {
    return stops_.colors_are_opaque && stops_.tile_mode != TileMode::kDecal;
  }

  bool AsLuminanceColor(Color4f* color) const override {
    if (color) {
      // SkGradientBaseShader::onAsLuminanceColor averages the normalized
      // stop colors without weighting their alpha or interval lengths.
      Color4f average{0, 0, 0, 1};
      for (const Color4f& stop : stops_.colors) {
        average.r += stop.r;
        average.g += stop.g;
        average.b += stop.b;
      }
      const float scale = 1.0f / static_cast<float>(stops_.colors.size());
      average.r *= scale;
      average.g *= scale;
      average.b *= scale;
      *color = average;
    }
    return true;
  }

protected:
  // Returns false if the point is masked out.
  virtual bool PointToT(ScalarPoint p, float* t) const = 0;

private:
  PMColor4f ShadePoint(ScalarPoint p, float paint_alpha) const {
    float t;
    if (!PointToT(p, &t)) {
      return {0, 0, 0, 0};
    }

    switch (stops_.tile_mode) {
    case TileMode::kMirror:
      t = std::clamp(std::abs((t - 1.0f) - 2 * std::floor((t - 1.0f) * 0.5f) - 1.0f), 0.0f, 1.0f);
      break;
    case TileMode::kRepeat:
      t = std::clamp(t - std::floor(t), 0.0f, 1.0f);
      break;
    case TileMode::kDecal:
      // The decal limit is the float just above 1.
      if (!(t >= 0 && t < std::nextafter(1.0f, 2.0f))) {
        return {0, 0, 0, 0};
      }
      [[fallthrough]];
    case TileMode::kClamp:
      // We clamp only when the stops are evenly spaced. If not, there may be
      // hard stops, and clamping ruins hard stops at 0 and/or 1. In that case,
      // the general lookup handles unclamped t.
      if (stops_.positions.empty()) {
        t = std::clamp(t, 0.0f, 1.0f);
      }
      break;
    }
    if (std::isnan(t)) {
      t = 0;
    }

    Color4f color = lookup_.Lookup(t);
    // The colors are interpolated unpremultiplied, then premultiplied unless
    // they are all opaque.
    PMColor4f result = stops_.colors_are_opaque ? color : color.Premul();
    result = result * paint_alpha;
    return ClampGamut(result);
  }

  GradientStops stops_;
  GradientLookup lookup_;
  ScalarMatrix points_to_unit_;
};

class LinearGradient final : public GradientShaderBase {
public:
  LinearGradient(const ScalarPoint pts[2], GradientStops stops)
      : GradientShaderBase(std::move(stops), LinearPointsToUnit(pts)) {
  }

protected:
  bool PointToT(ScalarPoint p, float* t) const override {
    *t = p.x;
    return true;
  }
};

class RadialGradient final : public GradientShaderBase {
public:
  RadialGradient(const ScalarPoint& center, float radius, GradientStops stops)
      : GradientShaderBase(std::move(stops), RadialToUnit(center, radius)) {
  }

protected:
  bool PointToT(ScalarPoint p, float* t) const override {
    *t = std::sqrt(p.x * p.x + p.y * p.y);
    return true;
  }
};

// SkConicalGradient, including its radial/strip/focal classification and
// the separate raster pipeline equations and masks for each case.
class ConicalGradient final : public GradientShaderBase {
  enum class Type { kRadial, kStrip, kFocal };

  struct FocalData {
    float r1 = 0;
    float focal_x = 0;
    bool is_swapped = false;

    bool IsFocalOnCircle() const {
      return NearlyZero(1 - r1);
    }
    bool IsWellBehaved() const {
      return !IsFocalOnCircle() && r1 > 1;
    }
    bool IsNativelyFocal() const {
      return NearlyZero(focal_x);
    }

    bool Set(float start_radius, float end_radius, ScalarMatrix* matrix) {
      focal_x = start_radius / (start_radius - end_radius);
      if (NearlyZero(focal_x - 1)) {
        matrix->PostTranslate(-1, 0);
        matrix->PostScale(-1, 1);
        std::swap(start_radius, end_radius);
        focal_x = 0;
        is_swapped = true;
      }

      ScalarMatrix focal_matrix;
      if (!MapToUnitX({focal_x, 0}, {1, 0}, &focal_matrix)) {
        return false;
      }
      matrix->PostConcat(focal_matrix);
      r1 = end_radius / std::abs(1 - focal_x);

      if (IsFocalOnCircle()) {
        matrix->PostScale(0.5f, 0.5f);
      } else {
        matrix->PostScale(r1 / (r1 * r1 - 1), 1 / std::sqrt(std::abs(r1 * r1 - 1)));
      }
      matrix->PostScale(std::abs(1 - focal_x), std::abs(1 - focal_x));
      return true;
    }
  };

public:
  static std::shared_ptr<const Shader> Create(const ScalarPoint& start, float start_radius,
                                               const ScalarPoint& end, float end_radius,
                                               GradientStops stops) {
    const float center_distance = Length(start.x - end.x, start.y - end.y);
    ScalarMatrix matrix;
    Type type;
    if (NearlyZero(center_distance)) {
      if (NearlyZero(std::max(start_radius, end_radius)) || NearlyEqual(start_radius, end_radius)) {
        return nullptr;
      }
      matrix = RadialToUnit(end, std::max(start_radius, end_radius));
      type = Type::kRadial;
    } else {
      if (!MapToUnitX(start, end, &matrix)) {
        return nullptr;
      }
      type = NearlyZero(end_radius - start_radius) ? Type::kStrip : Type::kFocal;
    }

    FocalData focal;
    if (type == Type::kFocal && !focal.Set(start_radius / center_distance, end_radius / center_distance, &matrix)) {
      return nullptr;
    }
    return std::make_shared<ConicalGradient>(start_radius, end_radius, center_distance,
                                              std::move(stops), type, matrix, focal);
  }

  ConicalGradient(float start_radius, float end_radius, float center_distance,
                    GradientStops stops, Type type, const ScalarMatrix& matrix, const FocalData& focal)
      : GradientShaderBase(std::move(stops), matrix),
        start_radius_(start_radius),
        end_radius_(end_radius),
        center_distance_(center_distance),
        type_(type),
        focal_(focal) {
  }

  bool IsOpaque() const override {
    // Areas outside the cone can remain transparent with opaque stop colors.
    return false;
  }

protected:
  bool PointToT(ScalarPoint p, float* t) const override {
    if (type_ == Type::kRadial) {
      const float delta_radius = end_radius_ - start_radius_;
      const float scale = std::max(start_radius_, end_radius_) / delta_radius;
      const float bias = -start_radius_ / delta_radius;
      *t = std::sqrt(p.x * p.x + p.y * p.y) * scale + bias;
      return true;
    }
    if (type_ == Type::kStrip) {
      const float scaled_radius = start_radius_ / center_distance_;
      *t = p.x + std::sqrt(scaled_radius * scaled_radius - p.y * p.y);
      return !std::isnan(*t);
    }

    if (focal_.IsFocalOnCircle()) {
      *t = p.x + p.y * p.y / p.x;
    } else if (focal_.IsWellBehaved()) {
      *t = std::sqrt(p.x * p.x + p.y * p.y) - p.x * (1 / focal_.r1);
    } else if (focal_.is_swapped || 1 - focal_.focal_x < 0) {
      *t = -std::sqrt(p.x * p.x - p.y * p.y) - p.x * (1 / focal_.r1);
    } else {
      *t = std::sqrt(p.x * p.x - p.y * p.y) - p.x * (1 / focal_.r1);
    }

    // mask_2pt_conical_degenerates runs before focal compensation/unswapping.
    if (!focal_.IsWellBehaved() && (*t <= 0 || std::isnan(*t))) {
      return false;
    }
    if (1 - focal_.focal_x < 0) {
      *t = -*t;
    }
    if (!focal_.IsNativelyFocal()) {
      *t += focal_.focal_x;
    }
    if (focal_.is_swapped) {
      *t = 1 - *t;
    }
    return true;
  }

private:
  float start_radius_;
  float end_radius_;
  float center_distance_;
  Type type_;
  FocalData focal_;
};

class SweepGradient final : public GradientShaderBase {
public:
  SweepGradient(const ScalarPoint& center, float t0, float t1, GradientStops stops)
      : GradientShaderBase(std::move(stops), ScalarMatrix::Translate(-center.x, -center.y)),
        t_bias_(-t0), t_scale_(1 / (t1 - t0)) {
  }

protected:
  bool PointToT(ScalarPoint p, float* t) const override {
    // xy_to_unit_angle uses this seventh-degree approximation in both the
    // highp and lowp pipelines, including at hard stop boundaries.
    const float x_abs = std::abs(p.x);
    const float y_abs = std::abs(p.y);
    const float slope = std::min(x_abs, y_abs) / std::max(x_abs, y_abs);
    const float s = slope * slope;
    float phi = slope * (0.15912117063999176025390625f + s *
                            (-5.185396969318389892578125e-2f + s *
                                (2.476101927459239959716796875e-2f + s *
                                    (-7.0547382347285747528076171875e-3f))));
    if (x_abs < y_abs) phi = 0.25f - phi;
    if (p.x < 0) phi = 0.5f - phi;
    if (p.y < 0) phi = 1.0f - phi;
    if (std::isnan(phi)) {
      phi = 0.0f;
    }
    *t = (phi + t_bias_) * t_scale_;
    return true;
  }

private:
  float t_bias_;
  float t_scale_;
};

// average_gradient_color.
Color4f AverageGradientColor(std::span<const Color4f> colors, const float* pos) {
  // The gradient is a piecewise linear interpolation between colors. For a
  // given interval, the integral between the two endpoints is 0.5 * (ci + cj)
  // * (pj - pi), which provides that intervals average color. The overall
  // average color is thus the sum of each piece. The thing to keep in mind is
  // that the provided gradient definition may implicitly use p=0 and p=1.
  const int color_count = static_cast<int>(colors.size());
  Color4f blend{0, 0, 0, 0};
  const auto add = [&blend](const Color4f& c, float w) {
    blend.r += c.r * w;
    blend.g += c.g * w;
    blend.b += c.b * w;
    blend.a += c.a * w;
  };
  for (int i = 0; i < color_count - 1; ++i) {
    // Calculate the average color for the interval between pos(i) and
    // pos(i+1)
    const Color4f& c0 = colors[static_cast<std::size_t>(i)];
    const Color4f& c1 = colors[static_cast<std::size_t>(i + 1)];

    // when pos == null, there are colorCount uniformly distributed stops,
    // going from 0 to 1, so pos[i + 1] - pos[i] = 1/(colorCount-1)
    float w;
    if (pos) {
      // Match position fixing in the constructor, clamping positions outside
      // [0, 1] and forcing the sequence to be monotonic
      float p0 = std::clamp(pos[i], 0.f, 1.f);
      float p1 = std::clamp(pos[i + 1], p0, 1.f);
      w = p1 - p0;

      // And account for any implicit intervals at the start or end of the
      // positions
      if (i == 0) {
        if (p0 > 0.0f) {
          // The first color is fixed between p = 0 to pos[0], so 0.5*(ci +
          // cj)*(pj - pi) becomes 0.5*(c + c)*(pj - 0) = c * pj
          add(colors[0], p0);
        }
      }
      if (i == color_count - 2) {
        if (p1 < 1.f) {
          // The last color is fixed between pos[n-1] to p = 1, so 0.5*(ci +
          // cj)*(pj - pi) becomes 0.5*(c + c)*(1 - pi) = c * (1 - pi)
          add(colors[static_cast<std::size_t>(color_count - 1)], 1.f - p1);
        }
      }
    } else {
      w = 1.f / static_cast<float>(color_count - 1);
    }

    add(c1, 0.5f * w);
    add(c0, 0.5f * w);
  }
  return blend;
}

// SkGradientBaseShader::MakeDegenerateGradient.
std::shared_ptr<const Shader> MakeDegenerateGradient(std::span<const Color4f> colors, const float* pos, TileMode mode) {
  switch (mode) {
  case TileMode::kDecal:
    // normally this would reject the area outside of the interpolation
    // region, so since inside region is empty when the radii are equal, the
    // entire draw region is empty
    return MakeEmptyShader();
  case TileMode::kRepeat:
  case TileMode::kMirror:
    // repeat and mirror are treated the same: the border colors are never
    // visible, but approximate the final color as infinite repetitions of
    // the colors, so it can be represented as the average color of the
    // gradient.
    return MakeColorShader(AverageGradientColor(colors, pos));
  case TileMode::kClamp:
    // Depending on how the gradient shape degenerates, there may be a more
    // specialized fallback representation for the factories to use, but this
    // is a reasonable default.
    return MakeColorShader(colors.back());
  }
  return nullptr;
}

bool ValidGradient(std::span<const Color4f> colors) {
  return !colors.empty();
}

// EXPAND_1_COLOR: a single color is repeated so the gradient has two stops.
struct ExpandedColors {
  std::vector<Color4f> colors;
  const float* pos;
};

ExpandedColors Expand1Color(std::span<const Color4f> colors, const float* pos) {
  if (colors.size() == 1) {
    return {{colors[0], colors[0]}, nullptr};
  }
  return {std::vector<Color4f>(colors.begin(), colors.end()), pos};
}

// legacy_shader_can_handle.
bool LegacyShaderCanHandle(const ScalarMatrix& inv) {
  // Scale+translate methods are always present, but affine might not be:
  // SkOpts::S32_alpha_D32_filter_DXDY only exists with NEON.
#if !defined(__ARM_NEON) && !defined(_M_ARM64)
  if (!(inv.GetSkewX() == 0 && inv.GetSkewY() == 0)) {
    return false;
  }
#endif

  // legacy code uses SkFixed 32.32, so ensure the inverse doesn't map device
  // coordinates out of range.
  const float max_dev_coord = 32767.0f;
  ScalarRect src = ScalarRect::MakeXYWH(0, 0, max_dev_coord, max_dev_coord);
  inv.MapRect(&src);

  // take 1/4 of max signed 32bits so we have room to subtract local values
  const float max_fixed32dot32 = static_cast<float>(std::numeric_limits<std::int32_t>::max()) * 0.25f;
  const ScalarRect limit = ScalarRect::MakeLTRB(-max_fixed32dot32, -max_fixed32dot32,
                                                +max_fixed32dot32, +max_fixed32dot32);
  // SkRect::contains.
  if (!(!src.IsEmpty() && !limit.IsEmpty() && limit.left <= src.left && limit.top <= src.top &&
        limit.right >= src.right && limit.bottom >= src.bottom)) {
    return false;
  }

  // legacy shader impl should be able to handle these matrices
  return true;
}

// SkImageShader with kClamp tiling, sampled through the mipmap level chosen
// by SkMipmapAccessor.
class ImageShader final : public Shader {
public:
  ImageShader(std::shared_ptr<const Image> image, const SamplingOptions& sampling, const ScalarMatrix& local_matrix)
      : image_(std::move(image)), sampling_(sampling), local_matrix_(local_matrix) {
  }

  std::unique_ptr<Context> MakeContext(const ScalarMatrix& ctm, const Color4f& paint_color) const override {
    ScalarMatrix total = ctm;
    total.PreConcat(local_matrix_);
    ScalarMatrix inverse;
    if (!total.Invert(&inverse)) {
      return nullptr;
    }
    return std::make_unique<ImageContext>(*this, inverse, paint_color);
  }

  bool IsOpaque() const override {
    return false;
  }

  // SkShaderBase::makeContext and SkImageShader::onMakeContext. Images are
  // premultiplied, tiled with kClamp and in the destination color space.
  bool CanMakeLegacyContext(const ScalarMatrix& ctm) const override {
    if (image_->GetPixmap().GetColorType() != ColorType::kN32) {
      return false;
    }
    const bool supported = (sampling_.filter == FilterMode::kNearest && sampling_.mipmap == MipmapMode::kNone) ||
                           (sampling_.filter == FilterMode::kLinear && sampling_.mipmap == MipmapMode::kNone) ||
                           (sampling_.filter == FilterMode::kLinear && sampling_.mipmap == MipmapMode::kNearest);
    if (!supported) {
      return false;
    }
    // SkBitmapProcShader stores bitmap coordinates in a 16bit buffer, so it
    // can't handle bitmaps larger than 65535, backed off to 32767.
    if (image_->Width() > 32767 || image_->Height() > 32767) {
      return false;
    }
    ScalarMatrix total = ctm;
    total.PreConcat(local_matrix_);
    ScalarMatrix inverse;
    if (!total.Invert(&inverse)) {
      return false;
    }
    return LegacyShaderCanHandle(inverse);
  }

private:
  class ImageContext final : public Context {
  public:
    ImageContext(const ImageShader& shader, const ScalarMatrix& inverse, const Color4f& paint_color)
        : shader_(shader), inverse_(inverse), paint_color_(paint_color) {
      const Image& image = *shader.image_;
      upper_ = &image.GetPixmap();
      MipmapMode resolved_mode = shader.sampling_.mipmap;

      float level = 0;
      if (resolved_mode != MipmapMode::kNone) {
        // SkMatrix::decomposeScale.
        const float sx = Length(inverse.GetScaleX(), inverse.GetSkewY());
        const float sy = Length(inverse.GetSkewX(), inverse.GetScaleY());
        if (!std::isfinite(sx) || !std::isfinite(sy) || NearlyZero(sx) || NearlyZero(sy)) {
          resolved_mode = MipmapMode::kNone;
        } else {
          level = Mipmap::ComputeLevel(1 / sx, 1 / sy);
          if (level <= 0) {
            resolved_mode = MipmapMode::kNone;
            level = 0;
          }
        }
      }

      // Nearest mode uses this level, so we round to pick the nearest. In
      // linear mode we use this level as the lower of the two to interpolate
      // between, so we take the floor.
      const int level_num = resolved_mode == MipmapMode::kNearest ? FloatRoundToInt(level)
                                                                 : FloatSaturateToInt(std::floor(level));
      const float lower_weight = level - static_cast<float>(level_num);

      if (level_num > 0 || (resolved_mode == MipmapMode::kLinear && lower_weight > 0)) {
        const Mipmap* mips = image.GetMipmap();
        if (!mips) {
          resolved_mode = MipmapMode::kNone;
        } else {
          if (level_num > 0) {
            if (level_num - 1 < mips->CountLevels()) {
              upper_ = &mips->GetLevel(level_num - 1);
            } else {
              resolved_mode = MipmapMode::kNone;
            }
          }
          if (resolved_mode == MipmapMode::kLinear) {
            if (level_num < mips->CountLevels()) {
              lower_ = &mips->GetLevel(level_num);
              lower_weight_ = lower_weight;
            }
          }
        }
      }
    }

    void ShadeSpan(int x, int y, int count, PMColor4f* out) override {
      const Image& image = *shader_.image_;
      for (int i = 0; i < count; ++i) {
        const ScalarPoint p = inverse_.MapPoint({static_cast<float>(x + i) + 0.5f, static_cast<float>(y) + 0.5f});
        PMColor4f c = Sample(*upper_, image, p);
        if (lower_ && lower_weight_ > 0) {
          const PMColor4f lower = Sample(*lower_, image, p);
          c = {c.r + (lower.r - c.r) * lower_weight_, c.g + (lower.g - c.g) * lower_weight_,
               c.b + (lower.b - c.b) * lower_weight_, c.a + (lower.a - c.a) * lower_weight_};
        }
        if (image.IsAlphaOnly()) {
          // Alpha-only images are colorized by the paint color.
          const PMColor4f paint = paint_color_.Premul();
          c = paint * c.a;
        } else {
          c = c * paint_color_.a;
        }
        out[i] = ClampGamut(c);
      }
    }

  private:
    PMColor4f Sample(const Pixmap& pm, const Image& image, ScalarPoint p) const {
      // The level's pixels cover the base image's bounds.
      const float u = p.x * static_cast<float>(pm.Width()) / static_cast<float>(image.Width());
      const float v = p.y * static_cast<float>(pm.Height()) / static_cast<float>(image.Height());
      const int max_x = pm.Width() - 1;
      const int max_y = pm.Height() - 1;
      if (shader_.sampling_.filter == FilterMode::kNearest) {
        // ix_and_ptr clamps exclusively, then subtracts one ULP so exact
        // integer coordinates select the pixel on their left/top.
        const auto nearest_index = [](float coordinate, int dimension) {
          const float upper = std::bit_cast<float>(std::bit_cast<std::uint32_t>(static_cast<float>(dimension)) - 1);
          const float clamped = std::min(std::max(std::numeric_limits<float>::min(), coordinate), upper);
          const float biased = std::bit_cast<float>(std::bit_cast<std::uint32_t>(clamped) - 1);
          return FloatSaturateToInt(biased);
        };
        const int ix = nearest_index(u, pm.Width());
        const int iy = nearest_index(v, pm.Height());
        return pm.GetPMColor4f(ix, iy);
      }
      const float fx0 = u - 0.5f;
      const float fy0 = v - 0.5f;
      const float x0 = std::floor(fx0);
      const float y0 = std::floor(fy0);
      const float fx = fx0 - x0;
      const float fy = fy0 - y0;
      const int ix0 = std::clamp(FloatSaturateToInt(x0), 0, max_x);
      const int ix1 = std::clamp(FloatSaturateToInt(x0) + 1, 0, max_x);
      const int iy0 = std::clamp(FloatSaturateToInt(y0), 0, max_y);
      const int iy1 = std::clamp(FloatSaturateToInt(y0) + 1, 0, max_y);
      const PMColor4f c00 = pm.GetPMColor4f(ix0, iy0);
      const PMColor4f c10 = pm.GetPMColor4f(ix1, iy0);
      const PMColor4f c01 = pm.GetPMColor4f(ix0, iy1);
      const PMColor4f c11 = pm.GetPMColor4f(ix1, iy1);
      const auto lerp = [](const PMColor4f& a, const PMColor4f& b, float w) {
        return PMColor4f{a.r + (b.r - a.r) * w, a.g + (b.g - a.g) * w, a.b + (b.b - a.b) * w, a.a + (b.a - a.a) * w};
      };
      return lerp(lerp(c00, c10, fx), lerp(c01, c11, fx), fy);
    }

    const ImageShader& shader_;
    ScalarMatrix inverse_;
    Color4f paint_color_;
    const Pixmap* upper_ = nullptr;
    const Pixmap* lower_ = nullptr;
    float lower_weight_ = 0;
  };

  std::shared_ptr<const Image> image_;
  SamplingOptions sampling_;
  ScalarMatrix local_matrix_;
};

// SkLocalMatrixShader. Nested local matrix shaders are not unfurled: the
// concatenation they compute is the same.
class LocalMatrixShader final : public Shader {
public:
  LocalMatrixShader(std::shared_ptr<const Shader> wrapped_shader, const ScalarMatrix& local_matrix)
      : wrapped_shader_(std::move(wrapped_shader)), local_matrix_(local_matrix) {
  }

  std::unique_ptr<Context> MakeContext(const ScalarMatrix& ctm, const Color4f& paint_color) const override {
    // SkShaderBase::ConcatLocalMatrices.
    ScalarMatrix total = ctm;
    total.PreConcat(local_matrix_);
    return wrapped_shader_->MakeContext(total, paint_color);
  }

  bool IsOpaque() const override {
    return wrapped_shader_->IsOpaque();
  }

  bool CanMakeLegacyContext(const ScalarMatrix& ctm) const override {
    ScalarMatrix total = ctm;
    total.PreConcat(local_matrix_);
    return wrapped_shader_->CanMakeLegacyContext(total);
  }

  bool AsLuminanceColor(Color4f* color) const override {
    return wrapped_shader_->AsLuminanceColor(color);
  }

private:
  std::shared_ptr<const Shader> wrapped_shader_;
  ScalarMatrix local_matrix_;
};

} // namespace

Shader::~Shader() = default;
Shader::Context::~Context() = default;

std::shared_ptr<const Shader> MakeColorShader(const Color4f& color) {
  // SkShaders::Color pins the alpha, as SkColor4f::pinAlpha.
  Color4f pinned = color;
  pinned.a = std::clamp(pinned.a, 0.0f, 1.0f);
  return std::make_shared<ColorShader>(pinned);
}

std::shared_ptr<const Shader> MakeEmptyShader() {
  return std::make_shared<EmptyShader>();
}

std::shared_ptr<const Shader> MakeWithLocalMatrix(std::shared_ptr<const Shader> shader,
                                                  const ScalarMatrix& local_matrix) {
  if (!shader) {
    return nullptr;
  }
  return std::make_shared<LocalMatrixShader>(std::move(shader), local_matrix);
}

std::shared_ptr<const Shader> GradientShader::MakeLinear(const ScalarPoint pts[2],
                                                         std::span<const Color4f> colors,
                                                         const float* pos,
                                                         TileMode mode) {
  if (!pts || !std::isfinite(Length(pts[1].x - pts[0].x, pts[1].y - pts[0].y))) {
    return nullptr;
  }
  if (!ValidGradient(colors)) {
    return nullptr;
  }
  if (1 == colors.size()) {
    return MakeColorShader(colors[0]);
  }

  if (NearlyZero(Length(pts[1].x - pts[0].x, pts[1].y - pts[0].y), kDegenerateThreshold)) {
    // Degenerate gradient, the only tricky complication is when in clamp
    // mode, the limit of the gradient approaches two half planes of solid
    // color (first and last). However, they are divided by the line
    // perpendicular to the start and end point, which becomes undefined once
    // start and end are exactly the same, so just use the end color for a
    // stable solution.
    return MakeDegenerateGradient(colors, pos, mode);
  }

  return std::make_shared<LinearGradient>(pts, NormalizeStops(colors, pos, mode));
}

std::shared_ptr<const Shader> GradientShader::MakeRadial(const ScalarPoint& center, float radius,
                                                         std::span<const Color4f> colors,
                                                         const float* pos,
                                                         TileMode mode) {
  if (radius < 0) {
    return nullptr;
  }
  if (!ValidGradient(colors)) {
    return nullptr;
  }
  if (1 == colors.size()) {
    return MakeColorShader(colors[0]);
  }

  if (NearlyZero(radius, kDegenerateThreshold)) {
    // Degenerate gradient optimization, and no special logic needed for
    // clamped radial gradient
    return MakeDegenerateGradient(colors, pos, mode);
  }

  return std::make_shared<RadialGradient>(center, radius, NormalizeStops(colors, pos, mode));
}

std::shared_ptr<const Shader> GradientShader::MakeTwoPointConical(const ScalarPoint& start, float start_radius,
                                                                  const ScalarPoint& end, float end_radius,
                                                                  std::span<const Color4f> colors,
                                                                  const float* pos,
                                                                  TileMode mode) {
  if (start_radius < 0 || end_radius < 0) {
    return nullptr;
  }
  if (!ValidGradient(colors)) {
    return nullptr;
  }
  if (NearlyZero(Length(start.x - end.x, start.y - end.y), kDegenerateThreshold)) {
    // If the center positions are the same, then the gradient is the radial
    // variant of a 2 pt conical gradient, an actual radial gradient
    // (startRadius == 0), or it is fully degenerate (startRadius ==
    // endRadius).
    if (NearlyEqual(start_radius, end_radius, kDegenerateThreshold)) {
      // Degenerate case, where the interpolation region area approaches
      // zero. The proper behavior depends on the tile mode, which is
      // consistent with the default degenerate gradient behavior, except when
      // mode = clamp and the radii > 0.
      if (mode == TileMode::kClamp && end_radius > kDegenerateThreshold) {
        // The interpolation region becomes an infinitely thin ring at the
        // radius, so the final gradient will be the first color repeated from
        // p=0 to 1, and then a hard stop switching to the last color at p=1.
        static constexpr float kCirclePos[3] = {0, 1, 1};
        const Color4f re_colors[3] = {colors[0], colors[0], colors.back()};
        return MakeRadial(start, end_radius, re_colors, kCirclePos, mode);
      } else {
        // Otherwise use the default degenerate case
        return MakeDegenerateGradient(colors, pos, mode);
      }
    } else if (NearlyZero(start_radius, kDegenerateThreshold)) {
      // We can treat this gradient as radial, which is faster. If we got
      // here, we know that endRadius is not equal to 0, so this produces a
      // meaningful gradient
      return MakeRadial(start, end_radius, colors, pos, mode);
    }
    // Else it's the 2pt conical radial variant with no degenerate radii, so
    // fall through to the regular 2pt constructor.
  }

  ExpandedColors expanded = Expand1Color(colors, pos);
  return ConicalGradient::Create(start, start_radius, end, end_radius,
                                  NormalizeStops(expanded.colors, expanded.pos, mode));
}

std::shared_ptr<const Shader> GradientShader::MakeSweep(float cx, float cy,
                                                        std::span<const Color4f> colors,
                                                        const float* pos,
                                                        TileMode mode,
                                                        float start_angle, float end_angle) {
  if (!ValidGradient(colors)) {
    return nullptr;
  }
  if (1 == colors.size()) {
    return MakeColorShader(colors[0]);
  }
  if (!std::isfinite(start_angle) || !std::isfinite(end_angle) || start_angle > end_angle) {
    return nullptr;
  }

  if (NearlyEqual(start_angle, end_angle, kDegenerateThreshold)) {
    // Degenerate gradient, which should follow default degenerate behavior
    // unless it is clamped and the angle is greater than 0.
    if (mode == TileMode::kClamp && end_angle > kDegenerateThreshold) {
      // In this case, the first color is repeated from 0 to the angle, then a
      // hardstop switches to the last color (all other colors are compressed
      // to the infinitely thin interpolation region).
      static constexpr float kClampPos[3] = {0, 1, 1};
      const Color4f re_colors[3] = {colors[0], colors[0], colors.back()};
      return MakeSweep(cx, cy, re_colors, kClampPos, mode, 0, end_angle);
    } else {
      return MakeDegenerateGradient(colors, pos, mode);
    }
  }

  if (start_angle <= 0 && end_angle >= 360) {
    // If the t-range includes [0,1], then we can always use clamping
    // (presumably faster).
    mode = TileMode::kClamp;
  }

  const float t0 = start_angle / 360;
  const float t1 = end_angle / 360;

  return std::make_shared<SweepGradient>(ScalarPoint{cx, cy}, t0, t1, NormalizeStops(colors, pos, mode));
}

std::shared_ptr<const Shader> MakeImageShader(std::shared_ptr<const Image> image,
                                              const SamplingOptions& sampling,
                                              const ScalarMatrix& local_matrix) {
  if (!image || image->Width() <= 0 || image->Height() <= 0) {
    return MakeEmptyShader();
  }
  return std::make_shared<ImageShader>(std::move(image), sampling, local_matrix);
}

} // namespace bkit
