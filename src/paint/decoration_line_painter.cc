// Ported from: blink/renderer/core/paint/decoration_line_painter.cc
// Ported from: blink/renderer/platform/graphics/styled_stroke_data.cc
#include "decoration_line_painter.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "path_effect.h"
#include "picture.h"
#include "shader.h"

namespace bkit {
namespace {
float SelectBestDashGap(float length, float dash, float gap) {
  const float min_dashes = std::floor((length + gap) / (dash + gap));
  const float max_dashes = min_dashes + 1;
  const float min_gap = (length - min_dashes * dash) / (min_dashes - 1);
  const float max_gap = (length - max_dashes * dash) / (max_dashes - 1);
  return max_gap <= 0 || std::abs(min_gap - gap) < std::abs(max_gap - gap) ? min_gap : max_gap;
}
bool StrokeIsDashed(float thickness, ETextDecorationStyle style) {
  return style == ETextDecorationStyle::kDashed || (style == ETextDecorationStyle::kDotted && thickness <= 3);
}
void SetupDash(PlatformPaint* paint, ETextDecorationStyle style, float thickness, float length) {
  float intervals[2];
  if (StrokeIsDashed(thickness, style)) {
    float dash = thickness, gap = thickness;
    if (style == ETextDecorationStyle::kDashed) {
      dash *= thickness >= 3 ? 2 : 3;
      gap *= thickness >= 3 ? 1 : 2;
    }
    if (length <= 2 * dash) return;
    if (length <= 2 * dash + gap) {
      const float multiplier = length / (2 * dash + gap);
      dash *= multiplier;
      gap *= multiplier;
    } else if (style == ETextDecorationStyle::kDashed) {
      gap = SelectBestDashGap(length, dash, gap);
    }
    intervals[0] = dash;
    intervals[1] = gap;
  } else {
    intervals[0] = 0;
    intervals[1] = length < 2 * thickness ? 2 * thickness :
        SelectBestDashGap(length, thickness, thickness) + thickness - 1.0e-2f;
    paint->SetStrokeCap(StrokeCap::kRound);
  }
  paint->SetPathEffect(DashPathEffect::Make(intervals, 0));
}
std::pair<ScalarPoint, ScalarPoint> SnappedPoints(const ScalarRect& line) {
  const float y = std::floor(line.top + std::max(line.Height() / 2, 0.5f));
  return {{std::trunc(line.left), y}, {std::trunc(line.right), y}};
}
ScalarPath WavyPath(const WaveDefinition& wave) {
  ScalarPath path;
  path.MoveTo({wave.phase, 0.5f});
  for (int i = 0; i < 3; ++i) {
    const float x = wave.phase + i * wave.wavelength;
    path.CubicTo({x + wave.wavelength * 0.5f, 0.5f + wave.control_point_distance},
                  {x + wave.wavelength * 0.5f, 0.5f - wave.control_point_distance},
                  {x + wave.wavelength, 0.5f});
  }
  return path;
}
struct WavyGeometry {
  ScalarPath path;
  ScalarRect pattern;
  float thickness;
  mutable Color4f tile_color;
  mutable std::shared_ptr<const Shader> tile_shader;
  explicit WavyGeometry(const DecorationGeometry& geometry) : path(WavyPath(geometry.wave)), thickness(geometry.Thickness()) {
    StrokeRec stroke(StrokeRec::kFill_InitStyle);
    stroke.SetStrokeStyle(geometry.Thickness());
    ScalarPath outline;
    stroke.ApplyToPath(&outline, path);
    const ScalarRect bounds = outline.ComputeTightBounds();
    pattern = ScalarRect::MakeLTRB(0, std::floor(bounds.top), geometry.wave.wavelength, std::ceil(bounds.bottom));
  }
  ScalarRect PaintRect(const DecorationGeometry& geometry) const {
    return ScalarRect::MakeXYWH(geometry.line.left, geometry.line.top + pattern.top + geometry.wavy_offset,
                                geometry.line.Width(), pattern.Height());
  }
  std::shared_ptr<const Shader> TileShader(Color4f color) const {
    if (!tile_shader || tile_color != color) {
      const ScalarRect tile = ScalarRect::MakeXYWH(0, 0, pattern.Width(), pattern.Height());
      PictureRecorder recorder;
      Canvas* recording = recorder.BeginRecording(tile);
      recording->Translate(-pattern.left, -pattern.top);
      PlatformPaint paint(color);
      paint.SetAntiAlias(true);
      paint.SetStyle(PlatformPaint::Style::kStroke);
      paint.SetStrokeWidth(thickness);
      recording->DrawPath(path, paint);
      tile_shader = MakePictureShader(recorder.FinishRecordingAsPicture(), tile, TileMode::kRepeat, TileMode::kDecal);
      tile_color = color;
    }
    return tile_shader;
  }
};

const WavyGeometry& GetWavyGeometry(const DecorationGeometry& geometry) {
  // Blink's single-entry WavyCache, per thread in this library. Bounds and
  // paint reuse the same stroked geometry; translations are not part of it.
  struct Cache {
    WaveDefinition wave;
    WavyGeometry geometry;
  };
  thread_local std::optional<Cache> cache;
  if (!cache || cache->wave != geometry.wave || cache->geometry.thickness != geometry.Thickness()) {
    cache.emplace(geometry.wave, WavyGeometry(geometry));
  }
  return cache->geometry;
}
} // namespace

DecorationGeometry DecorationGeometry::Make(ETextDecorationStyle style, ScalarRect line, float double_offset,
                                              float wavy_offset, const WaveDefinition* custom_wave) {
  DecorationGeometry geometry;
  geometry.style = style;
  geometry.line = line;
  geometry.double_offset = double_offset;
  geometry.wavy_offset = wavy_offset;
  if (style == ETextDecorationStyle::kWavy) {
    const float thickness = std::max(1.0f, line.Height());
    const float wavelength = 1 + 2 * std::round(2 * thickness + 0.5f);
    geometry.wave = custom_wave ? *custom_wave : WaveDefinition{wavelength, 0.5f + std::round(3 * thickness + 0.5f), -wavelength};
  }
  return geometry;
}
ScalarRect DecorationLinePainter::Bounds(const DecorationGeometry& geometry) {
  if (geometry.style == ETextDecorationStyle::kWavy) return GetWavyGeometry(geometry).PaintRect(geometry);
  if (geometry.style == ETextDecorationStyle::kDotted || geometry.style == ETextDecorationStyle::kDashed) {
    const float thickness = std::round(geometry.Thickness());
    const auto [start, end] = SnappedPoints(geometry.line);
    return ScalarRect::MakeXYWH(start.x, start.y - thickness / 2, end.x - start.x, thickness);
  }
  ScalarRect bounds = geometry.line;
  if (geometry.style == ETextDecorationStyle::kDouble) {
    bounds.top += std::min(0.0f, geometry.double_offset);
    bounds.bottom += std::max(0.0f, geometry.double_offset);
  }
  return bounds;
}
void DecorationLinePainter::Paint(PaintCanvas* canvas, const DecorationGeometry& geometry, Color4f color) {
  if (!(geometry.line.Width() > 0)) return;
  PlatformPaint paint(color);
  if (geometry.style == ETextDecorationStyle::kWavy) {
    const WavyGeometry& wave = GetWavyGeometry(geometry);
    PlatformPaint fill;
    fill.SetAntiAlias(true);
    fill.SetShader(wave.TileShader(color));
    const ScalarRect rect = wave.PaintRect(geometry);
    PaintCanvasAutoRestore restore(canvas, true);
    canvas->Translate(rect.left, rect.top);
    canvas->DrawRect(ScalarRect::MakeXYWH(0, 0, rect.Width(), rect.Height()), fill);
  } else if (geometry.style == ETextDecorationStyle::kDotted || geometry.style == ETextDecorationStyle::kDashed) {
    auto [start, end] = SnappedPoints(geometry.line);
    const float length = end.x - start.x;
    const int thickness = static_cast<int>(std::round(geometry.Thickness()));
    if (thickness % 2) { start.y += 0.5f; end.y += 0.5f; }
    if (!StrokeIsDashed(thickness, geometry.style)) {
      start.x += thickness / 2.0f;
      end.x -= thickness / 2.0f;
    }
    paint.SetStyle(PlatformPaint::Style::kStroke);
    paint.SetStrokeWidth(geometry.Thickness());
    paint.SetAntiAlias(geometry.antialias);
    SetupDash(&paint, geometry.style, thickness, length);
    canvas->DrawLine(start, end, paint);
  } else {
    const auto draw_rect = [&](float offset) {
      canvas->DrawRect(ScalarRect::MakeXYWH(geometry.line.left, std::floor(geometry.line.top + offset + 0.5f),
                                           geometry.line.Width(), std::max(std::floor(geometry.Thickness()), 1.0f)), paint);
    };
    draw_rect(0);
    if (geometry.style == ETextDecorationStyle::kDouble) draw_rect(geometry.double_offset);
  }
}
} // namespace bkit
