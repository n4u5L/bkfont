// Ported from: skia/src/ports/SkFontHost_FreeType_common.cpp

#include "font_host_freetype_common.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

#include <freetype/ftbitmap.h>
#ifdef FT_COLOR_H
#include <freetype/ftcolor.h>
#endif
#include <freetype/ftimage.h>
#include <freetype/ftoutln.h>
#include <freetype/ftsizes.h>
#if defined(FT_CONFIG_OPTION_SVG)
#include <freetype/otsvg.h>
#endif

#include "paint/canvas.h"
#include "paint/mask.h"
#include "opentype_svg_decoder.h"
#include "paint/pixmap.h"
#include "paint/raster_canvas.h"
#include "paint/scalar.h"
#include "paint/shader.h"

// FT_LOAD_COLOR and the corresponding FT_Pixel_Mode::FT_PIXEL_MODE_BGRA
// were introduced in FreeType 2.5.0.
#ifndef FT_LOAD_COLOR
#define FT_LOAD_COLOR (1L << 20)
#define FT_PIXEL_MODE_BGRA 7
#endif

// Keep the COLRv1 API version gate in sync with font_host_freetype.cc.
#ifdef TT_SUPPORT_COLRV1
#if (((FREETYPE_MAJOR) < 2) || ((FREETYPE_MAJOR) == 2 && (FREETYPE_MINOR) < 11) || ((FREETYPE_MAJOR) == 2 && (FREETYPE_MINOR) == 11 && (FREETYPE_PATCH) < 1)) && !defined(FT_STATIC_CAST)
#undef TT_SUPPORT_COLRV1
#endif
#endif

namespace bkit {

namespace {

class FTGeometrySink {
public:
  explicit FTGeometrySink(ScalarPath* path)
      : path_(path) {
  }

  static int Move(const FT_Vector* point, void* context) {
    auto& self = *static_cast<FTGeometrySink*>(context);
    if (self.started_) {
      self.path_->Close();
      self.started_ = false;
    }
    self.current_ = *point;
    return 0;
  }

  static int Line(const FT_Vector* point, void* context) {
    auto& self = *static_cast<FTGeometrySink*>(context);
    if (self.CurrentIsNot(point)) {
      self.GoingTo(point);
      self.path_->LineTo(ToPoint(*point));
    }
    return 0;
  }

  static int Quad(const FT_Vector* control, const FT_Vector* end, void* context) {
    auto& self = *static_cast<FTGeometrySink*>(context);
    if (self.CurrentIsNot(control) || self.CurrentIsNot(end)) {
      self.GoingTo(end);
      self.path_->QuadTo(ToPoint(*control), ToPoint(*end));
    }
    return 0;
  }

  static int Cubic(const FT_Vector* control1, const FT_Vector* control2, const FT_Vector* end, void* context) {
    auto& self = *static_cast<FTGeometrySink*>(context);
    if (self.CurrentIsNot(control1) || self.CurrentIsNot(control2) || self.CurrentIsNot(end)) {
      self.GoingTo(end);
      self.path_->CubicTo(ToPoint(*control1), ToPoint(*control2), ToPoint(*end));
    }
    return 0;
  }

  inline static constexpr FT_Outline_Funcs kFuncs{Move, Line, Quad, Cubic, 0, 0};

private:
  static ScalarPoint ToPoint(const FT_Vector& point) {
    return {FDot6ToFloat(point.x), -FDot6ToFloat(point.y)};
  }

  bool CurrentIsNot(const FT_Vector* point) const {
    return current_.x != point->x || current_.y != point->y;
  }

  void GoingTo(const FT_Vector* point) {
    if (!started_) {
      started_ = true;
      path_->MoveTo(ToPoint(current_));
    }
    current_ = *point;
  }

  ScalarPath* path_;
  bool started_ = false;
  FT_Vector current_{0, 0};
};

#ifdef TT_SUPPORT_COLRV1

struct FTSizeDeleter {
  void operator()(FT_Size size) const {
    FT_Done_Size(size);
  }
};

std::optional<ScalarPath> GenerateFacePathColrV1(FT_Face face, std::uint16_t glyph_id, const ScalarMatrix& matrix) {
  const FT_Int32 flags = FT_LOAD_BITMAP_METRICS_ONLY | FT_LOAD_NO_BITMAP |
                        FT_LOAD_NO_HINTING | FT_LOAD_NO_AUTOHINT | FT_LOAD_IGNORE_TRANSFORM;
  FT_Size size;
  if (FT_New_Size(face, &size)) return std::nullopt;
  std::unique_ptr<FT_SizeRec, FTSizeDeleter> unscaled_size(size);
  FT_Size old_size = face->size;
  ScalarPath path;

  // Load at units-per-em size, then apply the complete paint transform to the
  // outline points. The root paint already contains the requested face scale.
  const bool generated = !FT_Activate_Size(size) &&
                         !FT_Set_Char_Size(face, face->units_per_EM * 64, face->units_per_EM * 64, 72, 72) &&
                         !FT_Load_Glyph(face, glyph_id, flags) &&
                         ScalerContextFTUtils::GenerateGlyphPath(face, &path);
  FT_Activate_Size(old_size);
  if (!generated) return std::nullopt;

  path.Transform(matrix);
  return path;
}

struct OpaquePaintHash {
  std::size_t operator()(const FT_OpaquePaint& paint) const {
    return std::hash<FT_Byte*>{}(paint.p) ^ std::hash<FT_Bool>{}(paint.insert_root_transform);
  }
};

struct OpaquePaintEqual {
  bool operator()(const FT_OpaquePaint& a, const FT_OpaquePaint& b) const {
    return a.p == b.p && a.insert_root_transform == b.insert_root_transform;
  }
};

using ActivePaints = std::unordered_set<FT_OpaquePaint, OpaquePaintHash, OpaquePaintEqual>;

class AutoRemovePaint {
public:
  AutoRemovePaint(ActivePaints* paints, FT_OpaquePaint paint)
      : paints_(paints), paint_(paint) {
  }
  ~AutoRemovePaint() {
    paints_->erase(paint_);
  }

private:
  ActivePaints* paints_;
  FT_OpaquePaint paint_;
};

bool StartGlyphBounds(ScalarMatrix matrix, ScalarRect* bounds, FT_Face face,
                      std::uint16_t glyph_id, FT_Color_Root_Transform root_transform, ActivePaints* active_paints);

// Passing the matrix by value restores the parent's CTM at every return,
// including errors and sibling layer/composite branches.
bool TraversePaintBounds(ScalarMatrix matrix, ScalarRect* bounds, FT_Face face,
                         FT_OpaquePaint opaque_paint, ActivePaints* active_paints) {
  if (!active_paints->insert(opaque_paint).second) return false;
  AutoRemovePaint remove_paint(active_paints, opaque_paint);

  FT_COLR_Paint paint;
  if (!FT_Get_Paint(face, opaque_paint, &paint)) return false;

  ScalarMatrix transform;
  FT_OpaquePaint child;
  switch (paint.format) {
    case FT_COLR_PAINTFORMAT_COLR_LAYERS: {
      FT_LayerIterator& iterator = paint.u.colr_layers.layer_iterator;
      FT_OpaquePaint layer{nullptr, 1};
      while (FT_Get_Paint_Layers(face, &iterator, &layer)) {
        if (!TraversePaintBounds(matrix, bounds, face, layer, active_paints)) return false;
      }
      return true;
    }
    case FT_COLR_PAINTFORMAT_GLYPH: {
      auto path = GenerateFacePathColrV1(face, paint.u.glyph.glyphID, matrix);
      if (!path) return false;
      bounds->Join(path->GetBounds());
      // PaintGlyph clips its entire child paint. Bounds traversal stops here,
      // matching upstream, without validating or measuring the fill subgraph.
      return true;
    }
    case FT_COLR_PAINTFORMAT_COLR_GLYPH:
      return StartGlyphBounds(matrix, bounds, face, paint.u.colr_glyph.glyphID,
                              FT_COLOR_NO_ROOT_TRANSFORM, active_paints);
    case FT_COLR_PAINTFORMAT_TRANSFORM: {
      const FT_Affine23& affine = paint.u.transform.affine;
      transform = ScalarMatrix::MakeAll(FixedToFloat(affine.xx), -FixedToFloat(affine.xy), FixedToFloat(affine.dx),
                                        -FixedToFloat(affine.yx), FixedToFloat(affine.yy), -FixedToFloat(affine.dy));
      child = paint.u.transform.paint;
      break;
    }
    case FT_COLR_PAINTFORMAT_TRANSLATE:
      transform = ScalarMatrix::Translate(FixedToFloat(paint.u.translate.dx), -FixedToFloat(paint.u.translate.dy));
      child = paint.u.translate.paint;
      break;
    case FT_COLR_PAINTFORMAT_SCALE:
      transform.SetScale(FixedToFloat(paint.u.scale.scale_x), FixedToFloat(paint.u.scale.scale_y),
                         FixedToFloat(paint.u.scale.center_x), -FixedToFloat(paint.u.scale.center_y));
      child = paint.u.scale.paint;
      break;
    case FT_COLR_PAINTFORMAT_ROTATE:
      transform.SetRotate(-FixedToFloat(paint.u.rotate.angle) * 180,
                          FixedToFloat(paint.u.rotate.center_x), -FixedToFloat(paint.u.rotate.center_y));
      child = paint.u.rotate.paint;
      break;
    case FT_COLR_PAINTFORMAT_SKEW: {
      const float x_degrees = FixedToFloat(paint.u.skew.x_skew_angle) * 180;
      const float y_degrees = FixedToFloat(paint.u.skew.y_skew_angle) * 180;
      float x_tan = std::tan(x_degrees * (3.14159265358979323846f / 180));
      float y_tan = std::tan(-y_degrees * (3.14159265358979323846f / 180));
      if (std::abs(x_tan) <= kScalarNearlyZero) x_tan = 0;
      if (std::abs(y_tan) <= kScalarNearlyZero) y_tan = 0;
      transform.SetSkew(x_tan, y_tan, FixedToFloat(paint.u.skew.center_x), -FixedToFloat(paint.u.skew.center_y));
      child = paint.u.skew.paint;
      break;
    }
    case FT_COLR_PAINTFORMAT_COMPOSITE:
      return TraversePaintBounds(matrix, bounds, face, paint.u.composite.backdrop_paint, active_paints) &&
             TraversePaintBounds(matrix, bounds, face, paint.u.composite.source_paint, active_paints);
    case FT_COLR_PAINTFORMAT_SOLID:
    case FT_COLR_PAINTFORMAT_LINEAR_GRADIENT:
    case FT_COLR_PAINTFORMAT_RADIAL_GRADIENT:
    case FT_COLR_PAINTFORMAT_SWEEP_GRADIENT:
      return true;
    default:
      return false;
  }
  matrix.PreConcat(transform);
  return TraversePaintBounds(matrix, bounds, face, child, active_paints);
}

bool StartGlyphBounds(ScalarMatrix matrix, ScalarRect* bounds, FT_Face face,
                      std::uint16_t glyph_id, FT_Color_Root_Transform root_transform, ActivePaints* active_paints) {
  FT_OpaquePaint paint{nullptr, 1};
  return FT_Get_Color_Glyph_Paint(face, glyph_id, root_transform, &paint) &&
         TraversePaintBounds(matrix, bounds, face, paint, active_paints);
}

// -- COLRv1 drawing -----------------------------------------------------------

constexpr std::uint16_t kForegroundColorPaletteIndex = 0xFFFF;

// The stop_offset field is being upgraded to a larger representation in
// FreeType, and changed from 2.14 to 16.16. Adjust the shift factor depending
// on size type.
constexpr float kColorStopShift = sizeof(FT_ColorStop::stop_offset) == sizeof(FT_F2Dot14) ? 1 << 14 : 1 << 16;

std::optional<ScalarPath> GenerateFacePathColrV1(FT_Face face, std::uint16_t glyph_id) {
  return GenerateFacePathColrV1(face, glyph_id, ScalarMatrix());
}

// This linear interpolation is used for calculating a truncated color line in
// special edge cases. This interpolation needs to be kept in sync with what
// the gradient shader would normally do when truncating and drawing color
// lines. When drawing into N32 surfaces, this is expected to be true. If that
// changes, or if we support other color spaces in CPAL tables at some point,
// this needs to be looked at.
Color4f LerpColor(Color4f c0, Color4f c1, float t) {
  // Due to the floating point calculation in the caller, when interpolating
  // between very narrow stops, we may get values outside the interpolation
  // range, guard against these.
  if (t < 0) {
    return c0;
  }
  if (t > 1) {
    return c1;
  }
  return {c0.r + (c1.r - c0.r) * t, c0.g + (c1.g - c0.g) * t, c0.b + (c1.b - c0.b) * t, c0.a + (c1.a - c0.a) * t};
}

enum TruncateStops {
  kTruncateStart,
  kTruncateEnd
};

// Truncate a vector of color stops at a previously computed stop position and
// insert at that position the color interpolated between the surrounding
// stops.
void TruncateToStopInterpolating(float zero_radius_stop,
                                 std::vector<Color4f>& colors,
                                 std::vector<float>& stops,
                                 TruncateStops truncate_stops) {
  if (stops.size() <= 1u || zero_radius_stop < stops.front() || stops.back() < zero_radius_stop) {
    return;
  }

  std::size_t after_index = (truncate_stops == kTruncateStart)
                                ? static_cast<std::size_t>(std::lower_bound(stops.begin(), stops.end(), zero_radius_stop) - stops.begin())
                                : static_cast<std::size_t>(std::upper_bound(stops.begin(), stops.end(), zero_radius_stop) - stops.begin());

  const float t = (zero_radius_stop - stops[after_index - 1]) / (stops[after_index] - stops[after_index - 1]);
  Color4f lerp_color = LerpColor(colors[after_index - 1], colors[after_index], t);

  if (truncate_stops == kTruncateStart) {
    stops.erase(stops.begin(), stops.begin() + static_cast<std::ptrdiff_t>(after_index));
    colors.erase(colors.begin(), colors.begin() + static_cast<std::ptrdiff_t>(after_index));
    stops.insert(stops.begin(), 0);
    colors.insert(colors.begin(), lerp_color);
  } else {
    stops.erase(stops.begin() + static_cast<std::ptrdiff_t>(after_index), stops.end());
    colors.erase(colors.begin() + static_cast<std::ptrdiff_t>(after_index), colors.end());
    stops.insert(stops.end(), 1);
    colors.insert(colors.end(), lerp_color);
  }
}

float ColrV1AlphaToFloat(std::uint16_t alpha) {
  return (alpha / static_cast<float>(1 << 14));
}

TileMode ToTileMode(FT_PaintExtend extend_mode) {
  switch (extend_mode) {
  case FT_COLR_PAINT_EXTEND_REPEAT:
    return TileMode::kRepeat;
  case FT_COLR_PAINT_EXTEND_REFLECT:
    return TileMode::kMirror;
  default:
    return TileMode::kClamp;
  }
}

BlendMode ToBlendMode(FT_Composite_Mode composite_mode) {
  switch (composite_mode) {
  case FT_COLR_COMPOSITE_CLEAR:
    return BlendMode::kClear;
  case FT_COLR_COMPOSITE_SRC:
    return BlendMode::kSrc;
  case FT_COLR_COMPOSITE_DEST:
    return BlendMode::kDst;
  case FT_COLR_COMPOSITE_SRC_OVER:
    return BlendMode::kSrcOver;
  case FT_COLR_COMPOSITE_DEST_OVER:
    return BlendMode::kDstOver;
  case FT_COLR_COMPOSITE_SRC_IN:
    return BlendMode::kSrcIn;
  case FT_COLR_COMPOSITE_DEST_IN:
    return BlendMode::kDstIn;
  case FT_COLR_COMPOSITE_SRC_OUT:
    return BlendMode::kSrcOut;
  case FT_COLR_COMPOSITE_DEST_OUT:
    return BlendMode::kDstOut;
  case FT_COLR_COMPOSITE_SRC_ATOP:
    return BlendMode::kSrcATop;
  case FT_COLR_COMPOSITE_DEST_ATOP:
    return BlendMode::kDstATop;
  case FT_COLR_COMPOSITE_XOR:
    return BlendMode::kXor;
  case FT_COLR_COMPOSITE_PLUS:
    return BlendMode::kPlus;
  case FT_COLR_COMPOSITE_SCREEN:
    return BlendMode::kScreen;
  case FT_COLR_COMPOSITE_OVERLAY:
    return BlendMode::kOverlay;
  case FT_COLR_COMPOSITE_DARKEN:
    return BlendMode::kDarken;
  case FT_COLR_COMPOSITE_LIGHTEN:
    return BlendMode::kLighten;
  case FT_COLR_COMPOSITE_COLOR_DODGE:
    return BlendMode::kColorDodge;
  case FT_COLR_COMPOSITE_COLOR_BURN:
    return BlendMode::kColorBurn;
  case FT_COLR_COMPOSITE_HARD_LIGHT:
    return BlendMode::kHardLight;
  case FT_COLR_COMPOSITE_SOFT_LIGHT:
    return BlendMode::kSoftLight;
  case FT_COLR_COMPOSITE_DIFFERENCE:
    return BlendMode::kDifference;
  case FT_COLR_COMPOSITE_EXCLUSION:
    return BlendMode::kExclusion;
  case FT_COLR_COMPOSITE_MULTIPLY:
    return BlendMode::kMultiply;
  case FT_COLR_COMPOSITE_HSL_HUE:
    return BlendMode::kHue;
  case FT_COLR_COMPOSITE_HSL_SATURATION:
    return BlendMode::kSaturation;
  case FT_COLR_COMPOSITE_HSL_COLOR:
    return BlendMode::kColor;
  case FT_COLR_COMPOSITE_HSL_LUMINOSITY:
    return BlendMode::kLuminosity;
  default:
    return BlendMode::kDst;
  }
}

ScalarMatrix ToMatrix(const FT_Affine23& affine23) {
  // Convert from FreeType's FT_Affine23 column major order to row-major order.
  return ScalarMatrix::MakeAll(FixedToFloat(affine23.xx), -FixedToFloat(affine23.xy), FixedToFloat(affine23.dx),
                               -FixedToFloat(affine23.yx), FixedToFloat(affine23.yy), -FixedToFloat(affine23.dy));
}

float PointLength(ScalarPoint p) {
  return std::sqrt(p.x * p.x + p.y * p.y);
}

ScalarPoint Sub(ScalarPoint a, ScalarPoint b) {
  return {a.x - b.x, a.y - b.y};
}

ScalarPoint Add(ScalarPoint a, ScalarPoint b) {
  return {a.x + b.x, a.y + b.y};
}

ScalarPoint Scaled(ScalarPoint p, float s) {
  return {p.x * s, p.y * s};
}

// SkVectorProjection.
ScalarPoint VectorProjection(ScalarPoint a, ScalarPoint b) {
  float length = PointLength(b);
  if (!length) {
    return ScalarPoint();
  }
  // SkPoint::normalize.
  ScalarPoint b_normalized = Scaled(b, 1 / length);
  return Scaled(b_normalized, (a.x * b.x + a.y * b.y) / length);
}

bool Colrv1ConfigurePaint(FT_Face face,
                          std::span<ColorARGB> palette,
                          const ColorARGB foreground_color,
                          const FT_COLR_Paint& colr_paint,
                          PlatformPaint* paint) {
  auto fetch_color_stops = [&face, &palette, &foreground_color](
                               const FT_ColorStopIterator& color_stop_iterator,
                               std::vector<float>& stops,
                               std::vector<Color4f>& colors) -> bool {
    const FT_UInt color_stop_count = color_stop_iterator.num_color_stops;
    if (color_stop_count == 0) {
      return false;
    }

    // 5.7.11.2.4 ColorIndex, ColorStop and ColorLine
    // "Applications shall apply the colorStops in increasing stopOffset
    // order."
    struct ColorStop {
      float pos;
      Color4f color;
    };
    std::vector<ColorStop> color_stops_sorted;
    color_stops_sorted.resize(color_stop_count);

    FT_ColorStop ft_stop;
    FT_ColorStopIterator mutable_color_stop_iterator = color_stop_iterator;
    while (FT_Get_Colorline_Stops(face, &ft_stop, &mutable_color_stop_iterator)) {
      FT_UInt index = mutable_color_stop_iterator.current_color_stop - 1;
      ColorStop& sk_stop = color_stops_sorted[index];
      sk_stop.pos = static_cast<float>(ft_stop.stop_offset) / kColorStopShift;
      FT_UInt16& palette_index = ft_stop.color.palette_index;
      if (palette_index == kForegroundColorPaletteIndex) {
        sk_stop.color = Color4f::FromColor(foreground_color);
      } else if (palette_index >= palette.size()) {
        return false;
      } else {
        sk_stop.color = Color4f::FromColor(palette[palette_index]);
      }
      sk_stop.color.a *= ColrV1AlphaToFloat(static_cast<std::uint16_t>(ft_stop.color.alpha));
    }

    std::stable_sort(color_stops_sorted.begin(), color_stops_sorted.end(),
                     [](const ColorStop& a, const ColorStop& b) { return a.pos < b.pos; });

    stops.resize(color_stop_count);
    colors.resize(color_stop_count);
    for (std::size_t i = 0; i < color_stop_count; ++i) {
      stops[i] = color_stops_sorted[i].pos;
      colors[i] = color_stops_sorted[i].color;
    }
    return true;
  };

  switch (colr_paint.format) {
  case FT_COLR_PAINTFORMAT_SOLID: {
    FT_PaintSolid solid = colr_paint.u.solid;

    // Dont' draw anything with this color if the palette index is out of
    // bounds.
    Color4f color = kTransparentColor4f;
    if (solid.color.palette_index == kForegroundColorPaletteIndex) {
      color = Color4f::FromColor(foreground_color);
    } else if (solid.color.palette_index >= palette.size()) {
      return false;
    } else {
      color = Color4f::FromColor(palette[solid.color.palette_index]);
    }
    color.a *= ColrV1AlphaToFloat(static_cast<std::uint16_t>(solid.color.alpha));
    paint->SetShader(nullptr);
    paint->SetColor(color);
    return true;
  }
  case FT_COLR_PAINTFORMAT_LINEAR_GRADIENT: {
    const FT_PaintLinearGradient& linear_gradient = colr_paint.u.linear_gradient;
    std::vector<float> stops;
    std::vector<Color4f> colors;

    if (!fetch_color_stops(linear_gradient.colorline.color_stop_iterator, stops, colors)) {
      return false;
    }

    if (stops.size() == 1) {
      paint->SetColor(colors[0]);
      return true;
    }

    ScalarPoint line_positions[2] = {ScalarPoint{FixedToFloat(linear_gradient.p0.x), -FixedToFloat(linear_gradient.p0.y)},
                                     ScalarPoint{FixedToFloat(linear_gradient.p1.x), -FixedToFloat(linear_gradient.p1.y)}};
    ScalarPoint p0 = line_positions[0];
    ScalarPoint p1 = line_positions[1];
    ScalarPoint p2 = {FixedToFloat(linear_gradient.p2.x), -FixedToFloat(linear_gradient.p2.y)};

    // If p0p1 or p0p2 are degenerate probably nothing should be drawn. If
    // p0p1 and p0p2 are parallel then one side is the first color and the
    // other side is the last color, depending on the direction. For now,
    // just use the first color.
    const ScalarPoint p0p1 = Sub(p1, p0);
    const ScalarPoint p0p2 = Sub(p2, p0);
    if ((p1.x == p0.x && p1.y == p0.y) || (p2.x == p0.x && p2.y == p0.y) || !(p0p1.x * p0p2.y - p0p1.y * p0p2.x)) {
      paint->SetColor(colors[0]);
      return true;
    }

    // Follow implementation note in nanoemoji:
    // https://github.com/googlefonts/nanoemoji/blob/0ac6e7bb4d8202db692574d8530a9b643f1b3b3c/src/nanoemoji/svg.py#L188
    // to compute a new gradient end point P3 as the orthogonal projection of
    // the vector from p0 to p1 onto a line perpendicular to line p0p2 and
    // passing through p0.
    ScalarPoint perpendicular_to_p2p0 = Sub(p2, p0);
    perpendicular_to_p2p0 = {perpendicular_to_p2p0.y, -perpendicular_to_p2p0.x};
    ScalarPoint p3 = Add(p0, VectorProjection(Sub(p1, p0), perpendicular_to_p2p0));
    line_positions[1] = p3;

    // Project/scale points according to stop extrema along p0p3 line, p3
    // being the result of the projection above, then scale stops to to [0, 1]
    // range so that repeat modes work. The linear gradient shader performs
    // the repeat modes over the 0 to 1 range, that's why we need to scale the
    // stops to within that range.
    TileMode tile_mode = ToTileMode(linear_gradient.colorline.extend);
    float color_stop_range = stops.back() - stops.front();
    // If the color stops are all at the same offset position, repeat and
    // reflect modes become meaningless.
    if (color_stop_range == 0.f) {
      if (tile_mode != TileMode::kClamp) {
        paint->SetColor(kColorTransparent);
        return true;
      } else {
        // Insert duplicated fake color stop in pad case at +1.0f to enable
        // the projection of circles for an originally 0-length color stop
        // range. Adding this stop will paint the equivalent gradient,
        // because: All font specified color stops are in the same spot, mode
        // is pad, so everything before this spot is painted with the first
        // color, everything after this spot is painted with the last color.
        // Not adding this stop will skip the projection and result in
        // specifying non-normalized color stops to the shader.
        stops.push_back(stops.back() + 1.0f);
        colors.push_back(colors.back());
        color_stop_range = 1.0f;
      }
    }

    // If the colorStopRange is 0 at this point, the default behavior of the
    // shader is to clamp to 1 color stops that are above 1, clamp to 0 for
    // color stops that are below 0, and repeat the outer color stops at 0 and
    // 1 if the color stops are inside the range. That will result in the
    // correct rendering.
    if ((color_stop_range != 1 || stops.front() != 0.f)) {
      ScalarPoint p0p3 = Sub(p3, p0);
      ScalarPoint p0_offset = Scaled(p0p3, stops.front());
      ScalarPoint p1_offset = Scaled(p0p3, stops.back());

      line_positions[0] = Add(p0, p0_offset);
      line_positions[1] = Add(p0, p1_offset);

      float scale_factor = 1 / color_stop_range;
      float start_offset = stops.front();
      for (float& stop : stops) {
        stop = (stop - start_offset) * scale_factor;
      }
    }

    std::shared_ptr<const Shader> shader(GradientShader::MakeLinear(line_positions, colors, stops.data(), tile_mode));

    // An opaque color is needed to ensure the gradient is not modulated by
    // alpha.
    paint->SetColor(kColorBlack);
    paint->SetShader(shader);
    return true;
  }
  case FT_COLR_PAINTFORMAT_RADIAL_GRADIENT: {
    const FT_PaintRadialGradient& radial_gradient = colr_paint.u.radial_gradient;
    ScalarPoint start = {FixedToFloat(radial_gradient.c0.x), -FixedToFloat(radial_gradient.c0.y)};
    float start_radius = FixedToFloat(radial_gradient.r0);
    ScalarPoint end = {FixedToFloat(radial_gradient.c1.x), -FixedToFloat(radial_gradient.c1.y)};
    float end_radius = FixedToFloat(radial_gradient.r1);

    std::vector<float> stops;
    std::vector<Color4f> colors;
    if (!fetch_color_stops(radial_gradient.colorline.color_stop_iterator, stops, colors)) {
      return false;
    }

    if (stops.size() == 1) {
      paint->SetColor(colors[0]);
      return true;
    }

    float color_stop_range = stops.back() - stops.front();
    TileMode tile_mode = ToTileMode(radial_gradient.colorline.extend);

    if (color_stop_range == 0.f) {
      if (tile_mode != TileMode::kClamp) {
        paint->SetColor(kColorTransparent);
        return true;
      } else {
        // Insert duplicated fake color stop in pad case at +1.0f to enable
        // the projection of circles for an originally 0-length color stop
        // range. Adding this stop will paint the equivalent gradient,
        // because: All font specified color stops are in the same spot, mode
        // is pad, so everything before this spot is painted with the first
        // color, everything after this spot is painted with the last color.
        // Not adding this stop will skip the projection and result in
        // specifying non-normalized color stops to the shader.
        stops.push_back(stops.back() + 1.0f);
        colors.push_back(colors.back());
        color_stop_range = 1.0f;
      }
    }

    // If the colorStopRange is 0 at this point, the default behavior of the
    // shader is to clamp to 1 color stops that are above 1, clamp to 0 for
    // color stops that are below 0, and repeat the outer color stops at 0 and
    // 1 if the color stops are inside the range. That will result in the
    // correct rendering.
    if (color_stop_range != 1 || stops.front() != 0.f) {
      // For the two-point caonical shader to understand the COLRv1 color
      // stops we need to scale stops to 0 to 1 range and interpolate new
      // centers and radii. Otherwise the shader clamps stops outside the
      // range to 0 and 1 (larger interval) or repeats the outer stops at 0
      // and 1 if the (smaller interval).
      ScalarPoint start_to_end = Sub(end, start);
      float radius_diff = end_radius - start_radius;
      float scale_factor = 1 / color_stop_range;
      float stops_start_offset = stops.front();

      ScalarPoint start_offset = Scaled(start_to_end, stops.front());
      ScalarPoint end_offset = Scaled(start_to_end, stops.back());

      // The order of the following computations is important in order to
      // avoid overwriting start or startRadius before the second
      // reassignment.
      end = Add(start, end_offset);
      start = Add(start, start_offset);
      end_radius = start_radius + radius_diff * stops.back();
      start_radius = start_radius + radius_diff * stops.front();

      for (auto& stop : stops) {
        stop = (stop - stops_start_offset) * scale_factor;
      }
    }

    // For negative radii, interpolation is needed to prepare parameters
    // suitable for invoking the shader. Implementation below as resolution
    // discussed in https://github.com/googlefonts/colr-gradients-spec/issues/367.
    // Truncate to manually interpolated color for tile mode clamp, otherwise
    // calculate positive projected circles.
    if (start_radius < 0 || end_radius < 0) {
      if (start_radius == end_radius && start_radius < 0) {
        paint->SetColor(kColorTransparent);
        return true;
      }

      if (tile_mode == TileMode::kClamp) {
        ScalarPoint start_to_end = Sub(end, start);
        float radius_diff = end_radius - start_radius;
        float zero_radius_stop = 0.f;
        TruncateStops truncate_side = kTruncateStart;
        if (start_radius < 0) {
          truncate_side = kTruncateStart;

          // Compute color stop position where radius is = 0. After the
          // scaling of stop positions to the normal 0,1 range that we have
          // done above, the size of the radius as a function of the color
          // stops is: r(x) = r0 + x*(r1-r0) Solving this function for r(x) =
          // 0, we get: x = -r0 / (r1-r0)
          zero_radius_stop = -start_radius / (end_radius - start_radius);
          start_radius = 0.f;
          ScalarPoint start_end_diff = Scaled(Sub(end, start), zero_radius_stop);
          start = Add(start, start_end_diff);
        }

        if (end_radius < 0) {
          truncate_side = kTruncateEnd;
          zero_radius_stop = -start_radius / (end_radius - start_radius);
          end_radius = 0.f;
          ScalarPoint start_end_diff = Scaled(Sub(end, start), 1 - zero_radius_stop);
          end = Sub(end, start_end_diff);
        }

        if (!(start_radius == 0 && end_radius == 0)) {
          TruncateToStopInterpolating(zero_radius_stop, colors, stops, truncate_side);
        } else {
          // If both radii have become negative and where clamped to 0, we
          // need to produce a single color cone, otherwise the shader colors
          // the whole plane in a single color when two radii are specified as
          // 0.
          if (radius_diff > 0) {
            end = Add(start, start_to_end);
            end_radius = radius_diff;
            colors.erase(colors.begin(), colors.end() - 1);
            stops.erase(stops.begin(), stops.end() - 1);
          } else {
            start = Sub(start, start_to_end);
            start_radius = -radius_diff;
            colors.erase(colors.begin() + 1, colors.end());
            stops.erase(stops.begin() + 1, stops.end());
          }
        }
      } else {
        if (start_radius < 0 || end_radius < 0) {
          auto round_integer_multiple = [](float factor_zero_crossing, TileMode mode) {
            int rounded_multiple = factor_zero_crossing > 0
                                       ? static_cast<int>(std::ceil(factor_zero_crossing))
                                       : static_cast<int>(std::floor(factor_zero_crossing)) - 1;
            if (mode == TileMode::kMirror && rounded_multiple % 2 != 0) {
              rounded_multiple += rounded_multiple < 0 ? -1 : 1;
            }
            return rounded_multiple;
          };

          ScalarPoint start_to_end = Sub(end, start);
          float radius_diff = end_radius - start_radius;
          float factor_zero_crossing = (start_radius / (start_radius - end_radius));
          bool in_range = 0.f <= factor_zero_crossing && factor_zero_crossing <= 1.0f;
          float direction = in_range && radius_diff < 0 ? -1.0f : 1.0f;
          float circle_projection_factor = static_cast<float>(round_integer_multiple(factor_zero_crossing * direction, tile_mode));
          start_to_end = Scaled(start_to_end, circle_projection_factor);
          start_radius += circle_projection_factor * radius_diff;
          end_radius += circle_projection_factor * radius_diff;
          start = Add(start, start_to_end);
          end = Add(end, start_to_end);
        }
      }
    }

    // An opaque color is needed to ensure the gradient is not modulated by
    // alpha.
    paint->SetColor(kColorBlack);

    paint->SetShader(GradientShader::MakeTwoPointConical(start, start_radius, end, end_radius,
                                                         colors, stops.data(), tile_mode));

    return true;
  }
  case FT_COLR_PAINTFORMAT_SWEEP_GRADIENT: {
    const FT_PaintSweepGradient& sweep_gradient = colr_paint.u.sweep_gradient;
    ScalarPoint center = {FixedToFloat(sweep_gradient.center.x), -FixedToFloat(sweep_gradient.center.y)};

    float start_angle = FixedToFloat(sweep_gradient.start_angle * 180.0f);
    float end_angle = FixedToFloat(sweep_gradient.end_angle * 180.0f);
    // OpenType 1.9.1 adds a shift to the angle to ease specification of a 0
    // to 360 degree sweep.
    start_angle += 180.0f;
    end_angle += 180.0f;

    std::vector<float> stops;
    std::vector<Color4f> colors;
    if (!fetch_color_stops(sweep_gradient.colorline.color_stop_iterator, stops, colors)) {
      return false;
    }

    if (stops.size() == 1) {
      paint->SetColor(colors[0]);
      return true;
    }

    // An opaque color is needed to ensure the gradient is not modulated by
    // alpha.
    paint->SetColor(kColorBlack);

    // New (Var)SweepGradient implementation compliant with OpenType 1.9.1
    // from here.

    // The shader expects stops from 0 to 1, so we need to account for
    // minimum and maximum stop positions being different from 0 and 1. We do
    // that by scaling minimum and maximum stop positions to the 0 to 1
    // interval and scaling the angles inverse proportionally.

    // 1) Scale angles to their equivalent positions if stops were from 0 to
    // 1.

    float sector_angle = end_angle - start_angle;
    TileMode tile_mode = ToTileMode(sweep_gradient.colorline.extend);
    if (sector_angle == 0 && tile_mode != TileMode::kClamp) {
      // "If the ColorLine's extend mode is reflect or repeat and start and
      // end angle are equal, nothing is drawn.".
      paint->SetColor(kColorTransparent);
      return true;
    }

    float start_angle_scaled = start_angle + sector_angle * stops.front();
    float end_angle_scaled = start_angle + sector_angle * stops.back();

    // 2) Scale stops accordingly to 0 to 1 range.

    float color_stop_range = stops.back() - stops.front();
    if (color_stop_range == 0.f) {
      if (tile_mode != TileMode::kClamp) {
        paint->SetColor(kColorTransparent);
        return true;
      } else {
        // Insert duplicated fake color stop in pad case at +1.0f to feed the
        // shader correct values and enable painting a pad sweep gradient with
        // two colors. Adding this stop will paint the equivalent gradient,
        // because: All font specified color stops are in the same spot, mode
        // is pad, so everything before this spot is painted with the first
        // color, everything after this spot is painted with the last color.
        // Not adding this stop will skip the projection and result in
        // specifying non-normalized color stops to the shader.
        stops.push_back(stops.back() + 1.0f);
        colors.push_back(colors.back());
        color_stop_range = 1.0f;
      }
    }

    float scale_factor = 1 / color_stop_range;
    float start_offset = stops.front();

    for (float& stop : stops) {
      stop = (stop - start_offset) * scale_factor;
    }

    // https://docs.microsoft.com/en-us/typography/opentype/spec/colr#sweep-gradients
    // "The angles are expressed in counter-clockwise degrees from the
    // direction of the positive x-axis on the design grid. [...] The color
    // line progresses from the start angle to the end angle in the
    // counter-clockwise direction;" - Convert angles and stops from
    // counter-clockwise to clockwise for the shader if the gradient is not
    // already reversed due to start angle being larger than end angle.
    start_angle_scaled = 360.f - start_angle_scaled;
    end_angle_scaled = 360.f - end_angle_scaled;
    if (start_angle_scaled >= end_angle_scaled) {
      std::swap(start_angle_scaled, end_angle_scaled);
      std::reverse(stops.begin(), stops.end());
      std::reverse(colors.begin(), colors.end());
      for (auto& stop : stops) {
        stop = 1.0f - stop;
      }
    }

    paint->SetShader(GradientShader::MakeSweep(center.x, center.y, colors, stops.data(), tile_mode,
                                               start_angle_scaled, end_angle_scaled));

    return true;
  }
  default: {
    return false;
  }
  }
}

bool Colrv1DrawPaint(Canvas* canvas,
                     std::span<ColorARGB> palette,
                     const ColorARGB foreground_color,
                     FT_Face face,
                     const FT_COLR_Paint& colr_paint) {
  switch (colr_paint.format) {
  case FT_COLR_PAINTFORMAT_GLYPH: {
    auto path = GenerateFacePathColrV1(face, static_cast<std::uint16_t>(colr_paint.u.glyph.glyphID));
    if (!path) {
      return false;
    }
    canvas->ClipPath(*path, true /* do_anti_alias */);
    return true;
  }
  case FT_COLR_PAINTFORMAT_SOLID:
  case FT_COLR_PAINTFORMAT_LINEAR_GRADIENT:
  case FT_COLR_PAINTFORMAT_RADIAL_GRADIENT:
  case FT_COLR_PAINTFORMAT_SWEEP_GRADIENT: {
    PlatformPaint paint;
    if (!Colrv1ConfigurePaint(face, palette, foreground_color, colr_paint, &paint)) {
      return false;
    }
    canvas->DrawPaint(paint);
    return true;
  }
  case FT_COLR_PAINTFORMAT_TRANSFORM:
  case FT_COLR_PAINTFORMAT_TRANSLATE:
  case FT_COLR_PAINTFORMAT_SCALE:
  case FT_COLR_PAINTFORMAT_ROTATE:
  case FT_COLR_PAINTFORMAT_SKEW:
    [[fallthrough]]; // Transforms handled in Colrv1Transform.
  default:
    return false;
  }
}

bool Colrv1DrawGlyphWithPath(Canvas* canvas,
                             std::span<ColorARGB> palette, ColorARGB foreground_color,
                             FT_Face face,
                             const FT_COLR_Paint& glyph_paint, const FT_COLR_Paint& fill_paint) {
  PlatformPaint fill;
  fill.SetAntiAlias(true);
  if (!Colrv1ConfigurePaint(face, palette, foreground_color, fill_paint, &fill)) {
    return false;
  }

  auto path = GenerateFacePathColrV1(face, static_cast<std::uint16_t>(glyph_paint.u.glyph.glyphID));
  if (!path) {
    return false;
  }
  canvas->DrawPath(*path, fill);
  return true;
}

// Concatenates the transforms directly on the canvas.
void Colrv1Transform(const FT_COLR_Paint& colr_paint, Canvas* canvas) {
  ScalarMatrix transform;

  switch (colr_paint.format) {
  case FT_COLR_PAINTFORMAT_TRANSFORM: {
    transform = ToMatrix(colr_paint.u.transform.affine);
    break;
  }
  case FT_COLR_PAINTFORMAT_TRANSLATE: {
    transform = ScalarMatrix::Translate(FixedToFloat(colr_paint.u.translate.dx), -FixedToFloat(colr_paint.u.translate.dy));
    break;
  }
  case FT_COLR_PAINTFORMAT_SCALE: {
    transform.SetScale(FixedToFloat(colr_paint.u.scale.scale_x),
                       FixedToFloat(colr_paint.u.scale.scale_y),
                       FixedToFloat(colr_paint.u.scale.center_x),
                       -FixedToFloat(colr_paint.u.scale.center_y));
    break;
  }
  case FT_COLR_PAINTFORMAT_ROTATE: {
    // COLRv1 angles are counter-clockwise, compare
    // https://docs.microsoft.com/en-us/typography/opentype/spec/colr#formats-24-to-27-paintrotate-paintvarrotate-paintrotatearoundcenter-paintvarrotatearoundcenter
    transform.SetRotate(-FixedToFloat(colr_paint.u.rotate.angle) * 180.0f,
                        FixedToFloat(colr_paint.u.rotate.center_x),
                        -FixedToFloat(colr_paint.u.rotate.center_y));
    break;
  }
  case FT_COLR_PAINTFORMAT_SKEW: {
    // In the PAINTFORMAT_ROTATE implementation, SetRotate snaps to 0 for
    // values very close to 0. Do the same here.

    float x_deg = FixedToFloat(colr_paint.u.skew.x_skew_angle) * 180.0f;
    float x_rad = x_deg * (3.14159265358979323846f / 180);
    float x_tan = std::tan(x_rad);
    x_tan = std::abs(x_tan) <= kScalarNearlyZero ? 0.0f : x_tan;

    float y_deg = FixedToFloat(colr_paint.u.skew.y_skew_angle) * 180.0f;
    // Negate y_skew_angle due to the y-down coordinate system to achieve
    // counter-clockwise skew along the y-axis.
    float y_rad = -y_deg * (3.14159265358979323846f / 180);
    float y_tan = std::tan(y_rad);
    y_tan = std::abs(y_tan) <= kScalarNearlyZero ? 0.0f : y_tan;

    transform.SetSkew(x_tan, y_tan,
                      FixedToFloat(colr_paint.u.skew.center_x),
                      -FixedToFloat(colr_paint.u.skew.center_y));
    break;
  }
  default: {
    // Only transforms are handled in this function.
  }
  }
  canvas->Concat(transform);
}

bool Colrv1StartGlyph(Canvas* canvas,
                      std::span<ColorARGB> palette,
                      ColorARGB foreground_color,
                      FT_Face face,
                      std::uint16_t glyph_id,
                      FT_Color_Root_Transform root_transform,
                      ActivePaints* active_paints);

bool Colrv1TraversePaint(Canvas* canvas,
                         std::span<ColorARGB> palette,
                         const ColorARGB foreground_color,
                         FT_Face face,
                         FT_OpaquePaint opaque_paint,
                         ActivePaints* active_paints) {
  // Cycle detection, see section "5.7.11.1.9 Color glyphs as a directed
  // acyclic graph".
  if (active_paints->contains(opaque_paint)) {
    return true;
  }

  active_paints->insert(opaque_paint);
  AutoRemovePaint remove_paint(active_paints, opaque_paint);

  FT_COLR_Paint paint;
  if (!FT_Get_Paint(face, opaque_paint, &paint)) {
    return false;
  }

  AutoCanvasRestore auto_restore(canvas, true /* do_save */);
  switch (paint.format) {
  case FT_COLR_PAINTFORMAT_COLR_LAYERS: {
    FT_LayerIterator& layer_iterator = paint.u.colr_layers.layer_iterator;
    FT_OpaquePaint layer_paint{nullptr, 1};
    while (FT_Get_Paint_Layers(face, &layer_iterator, &layer_paint)) {
      if (!Colrv1TraversePaint(canvas, palette, foreground_color, face, layer_paint, active_paints)) {
        return false;
      }
    }
    return true;
  }
  case FT_COLR_PAINTFORMAT_GLYPH: {
    // Special case paint graph leaf situations to improve performance. These
    // are situations in the graph where a GlyphPaint is followed by either a
    // solid or a gradient fill. Here we can use DrawPath() + paint directly
    // which is faster than setting a ClipPath() followed by a DrawPaint().
    FT_COLR_Paint fill_paint;
    if (!FT_Get_Paint(face, paint.u.glyph.paint, &fill_paint)) {
      return false;
    }
    if (fill_paint.format == FT_COLR_PAINTFORMAT_SOLID ||
        fill_paint.format == FT_COLR_PAINTFORMAT_LINEAR_GRADIENT ||
        fill_paint.format == FT_COLR_PAINTFORMAT_RADIAL_GRADIENT ||
        fill_paint.format == FT_COLR_PAINTFORMAT_SWEEP_GRADIENT) {
      return Colrv1DrawGlyphWithPath(canvas, palette, foreground_color, face, paint, fill_paint);
    }
    if (!Colrv1DrawPaint(canvas, palette, foreground_color, face, paint)) {
      return false;
    }
    return Colrv1TraversePaint(canvas, palette, foreground_color, face, paint.u.glyph.paint, active_paints);
  }
  case FT_COLR_PAINTFORMAT_COLR_GLYPH:
    return Colrv1StartGlyph(canvas, palette, foreground_color, face, static_cast<std::uint16_t>(paint.u.colr_glyph.glyphID),
                            FT_COLOR_NO_ROOT_TRANSFORM, active_paints);
  case FT_COLR_PAINTFORMAT_TRANSFORM:
    Colrv1Transform(paint, canvas);
    return Colrv1TraversePaint(canvas, palette, foreground_color, face, paint.u.transform.paint, active_paints);
  case FT_COLR_PAINTFORMAT_TRANSLATE:
    Colrv1Transform(paint, canvas);
    return Colrv1TraversePaint(canvas, palette, foreground_color, face, paint.u.translate.paint, active_paints);
  case FT_COLR_PAINTFORMAT_SCALE:
    Colrv1Transform(paint, canvas);
    return Colrv1TraversePaint(canvas, palette, foreground_color, face, paint.u.scale.paint, active_paints);
  case FT_COLR_PAINTFORMAT_ROTATE:
    Colrv1Transform(paint, canvas);
    return Colrv1TraversePaint(canvas, palette, foreground_color, face, paint.u.rotate.paint, active_paints);
  case FT_COLR_PAINTFORMAT_SKEW:
    Colrv1Transform(paint, canvas);
    return Colrv1TraversePaint(canvas, palette, foreground_color, face, paint.u.skew.paint, active_paints);
  case FT_COLR_PAINTFORMAT_COMPOSITE: {
    AutoCanvasRestore acr(canvas, false);
    canvas->SaveLayer(nullptr, nullptr);
    if (!Colrv1TraversePaint(canvas, palette, foreground_color, face, paint.u.composite.backdrop_paint, active_paints)) {
      return false;
    }
    PlatformPaint blend_mode_paint;
    blend_mode_paint.SetBlendMode(ToBlendMode(paint.u.composite.composite_mode));
    canvas->SaveLayer(nullptr, &blend_mode_paint);
    return Colrv1TraversePaint(canvas, palette, foreground_color, face, paint.u.composite.source_paint, active_paints);
  }
  case FT_COLR_PAINTFORMAT_SOLID:
  case FT_COLR_PAINTFORMAT_LINEAR_GRADIENT:
  case FT_COLR_PAINTFORMAT_RADIAL_GRADIENT:
  case FT_COLR_PAINTFORMAT_SWEEP_GRADIENT: {
    return Colrv1DrawPaint(canvas, palette, foreground_color, face, paint);
  }
  default:
    return false;
  }
}

ScalarPath GetClipBoxPath(FT_Face face, std::uint16_t glyph_id, bool untransformed) {
  ScalarPath result_path;
  std::unique_ptr<FT_SizeRec, FTSizeDeleter> unscaled_ft_size;
  FT_Size old_size = face->size;
  FT_Matrix old_transform;
  FT_Vector old_delta;
  FT_Error err = 0;

  if (untransformed) {
    unscaled_ft_size.reset([face]() -> FT_Size {
      FT_Size size;
      FT_Error new_size_err = FT_New_Size(face, &size);
      if (new_size_err != 0) {
        return nullptr;
      }
      return size;
    }());
    if (!unscaled_ft_size) {
      return result_path;
    }

    err = FT_Activate_Size(unscaled_ft_size.get());
    if (err != 0) {
      return result_path;
    }

    // SkIntToFDot6.
    err = FT_Set_Char_Size(face, static_cast<FT_F26Dot6>(face->units_per_EM) << 6, 0, 0, 0);
    if (err != 0) {
      return result_path;
    }

    FT_Get_Transform(face, &old_transform, &old_delta);
    FT_Set_Transform(face, nullptr, nullptr);
  }

  FT_ClipBox colr_glyph_clip_box;
  if (FT_Get_Color_Glyph_ClipBox(face, glyph_id, &colr_glyph_clip_box)) {
    const ScalarPoint polygon[4] = {
        {FDot6ToFloat(colr_glyph_clip_box.bottom_left.x), -FDot6ToFloat(colr_glyph_clip_box.bottom_left.y)},
        {FDot6ToFloat(colr_glyph_clip_box.top_left.x), -FDot6ToFloat(colr_glyph_clip_box.top_left.y)},
        {FDot6ToFloat(colr_glyph_clip_box.top_right.x), -FDot6ToFloat(colr_glyph_clip_box.top_right.y)},
        {FDot6ToFloat(colr_glyph_clip_box.bottom_right.x), -FDot6ToFloat(colr_glyph_clip_box.bottom_right.y)}};
    result_path = ScalarPath::Polygon(polygon, true);
  }

  if (untransformed) {
    err = FT_Activate_Size(old_size);
    if (err != 0) {
      return result_path;
    }
    FT_Set_Transform(face, &old_transform, &old_delta);
  }

  return result_path;
}

bool Colrv1StartGlyph(Canvas* canvas,
                      std::span<ColorARGB> palette,
                      ColorARGB foreground_color,
                      FT_Face face,
                      std::uint16_t glyph_id,
                      FT_Color_Root_Transform root_transform,
                      ActivePaints* active_paints) {
  FT_OpaquePaint opaque_paint{nullptr, 1};
  if (!FT_Get_Color_Glyph_Paint(face, glyph_id, root_transform, &opaque_paint)) {
    return false;
  }

  bool untransformed = root_transform == FT_COLOR_NO_ROOT_TRANSFORM;
  ScalarPath clip_box_path = GetClipBoxPath(face, glyph_id, untransformed);
  if (!clip_box_path.IsEmpty()) {
    canvas->ClipPath(clip_box_path, true);
  }

  if (!Colrv1TraversePaint(canvas, palette, foreground_color, face, opaque_paint, active_paints)) {
    return false;
  }

  return true;
}

#endif // TT_SUPPORT_COLRV1

FT_Pixel_Mode ComputePixelMode(MaskFormat format) {
  switch (format) {
  case MaskFormat::kBW:
    return FT_PIXEL_MODE_MONO;
  case MaskFormat::kA8:
  default:
    return FT_PIXEL_MODE_GRAY;
  }
}

std::uint16_t PackTriple(unsigned r, unsigned g, unsigned b) {
  return Pack888ToRGB16(r, g, b);
}

std::uint16_t GrayToRGB16(unsigned gray) {
  return Pack888ToRGB16(gray, gray, gray);
}

int BitTst(const std::uint8_t data[], int bit_offset) {
  int low_bit = data[bit_offset >> 3] >> (~bit_offset & 7);
  return low_bit & 1;
}

// Copies a FT_Bitmap into a mask with the same dimensions.
//
// FT_PIXEL_MODE_MONO
// FT_PIXEL_MODE_GRAY
// FT_PIXEL_MODE_LCD
// FT_PIXEL_MODE_LCD_V
template <bool APPLY_PREBLEND>
void CopyFT2LCD16(const FT_Bitmap& bitmap, MaskBuilder* dst_mask, int lcd_is_bgr,
                  const std::uint8_t* table_r, const std::uint8_t* table_g, const std::uint8_t* table_b) {
  const std::uint8_t* src = bitmap.buffer;
  std::uint16_t* dst = reinterpret_cast<std::uint16_t*>(dst_mask->image);
  const std::size_t dst_rb = dst_mask->row_bytes;

  const int width = dst_mask->bounds.Width();
  const int height = dst_mask->bounds.Height();

  switch (bitmap.pixel_mode) {
  case FT_PIXEL_MODE_MONO:
    for (int y = height; y-- > 0;) {
      for (int x = 0; x < width; ++x) {
        dst[x] = static_cast<std::uint16_t>(-BitTst(src, x));
      }
      dst = reinterpret_cast<std::uint16_t*>(reinterpret_cast<char*>(dst) + dst_rb);
      src += bitmap.pitch;
    }
    break;
  case FT_PIXEL_MODE_GRAY:
    for (int y = height; y-- > 0;) {
      for (int x = 0; x < width; ++x) {
        dst[x] = GrayToRGB16(src[x]);
      }
      dst = reinterpret_cast<std::uint16_t*>(reinterpret_cast<char*>(dst) + dst_rb);
      src += bitmap.pitch;
    }
    break;
  case FT_PIXEL_MODE_LCD:
    for (int y = height; y-- > 0;) {
      const std::uint8_t* triple = src;
      if (lcd_is_bgr) {
        for (int x = 0; x < width; x++) {
          dst[x] = PackTriple(ApplyLutIf<APPLY_PREBLEND>(triple[2], table_r),
                              ApplyLutIf<APPLY_PREBLEND>(triple[1], table_g),
                              ApplyLutIf<APPLY_PREBLEND>(triple[0], table_b));
          triple += 3;
        }
      } else {
        for (int x = 0; x < width; x++) {
          dst[x] = PackTriple(ApplyLutIf<APPLY_PREBLEND>(triple[0], table_r),
                              ApplyLutIf<APPLY_PREBLEND>(triple[1], table_g),
                              ApplyLutIf<APPLY_PREBLEND>(triple[2], table_b));
          triple += 3;
        }
      }
      src += bitmap.pitch;
      dst = reinterpret_cast<std::uint16_t*>(reinterpret_cast<char*>(dst) + dst_rb);
    }
    break;
  case FT_PIXEL_MODE_LCD_V:
    for (int y = height; y-- > 0;) {
      const std::uint8_t* src_r = src;
      const std::uint8_t* src_g = src_r + bitmap.pitch;
      const std::uint8_t* src_b = src_g + bitmap.pitch;
      if (lcd_is_bgr) {
        using std::swap;
        swap(src_r, src_b);
      }
      for (int x = 0; x < width; x++) {
        dst[x] = PackTriple(ApplyLutIf<APPLY_PREBLEND>(*src_r++, table_r),
                            ApplyLutIf<APPLY_PREBLEND>(*src_g++, table_g),
                            ApplyLutIf<APPLY_PREBLEND>(*src_b++, table_b));
      }
      src += 3 * bitmap.pitch;
      dst = reinterpret_cast<std::uint16_t*>(reinterpret_cast<char*>(dst) + dst_rb);
    }
    break;
  default:
    // unsupported FT_Pixel_Mode for LCD16
    break;
  }
}

// Copies a FT_Bitmap into a mask with the same dimensions.
//
// Yes, No, Never Requested, Never Produced
//
//                       kBW kA8 k3D kARGB32 kLCD16
// FT_PIXEL_MODE_MONO     Y   Y  NR     N       Y
// FT_PIXEL_MODE_GRAY     N   Y  NR     N       Y
// FT_PIXEL_MODE_GRAY2   NP  NP  NR    NP      NP
// FT_PIXEL_MODE_GRAY4   NP  NP  NR    NP      NP
// FT_PIXEL_MODE_LCD     NP  NP  NR    NP      NP
// FT_PIXEL_MODE_LCD_V   NP  NP  NR    NP      NP
// FT_PIXEL_MODE_BGRA     N   N  NR     Y       N
//
// TODO: All of these N need to be Y or otherwise ruled out.
void CopyFTBitmap(const FT_Bitmap& src_ft_bitmap, MaskBuilder* dst_mask) {
  const std::uint8_t* src = reinterpret_cast<const std::uint8_t*>(src_ft_bitmap.buffer);
  const FT_Pixel_Mode src_format = static_cast<FT_Pixel_Mode>(src_ft_bitmap.pixel_mode);
  // FT_Bitmap::pitch is an int and allowed to be negative.
  const int src_pitch = src_ft_bitmap.pitch;
  const std::size_t src_row_bytes = static_cast<std::size_t>(std::abs(src_pitch));

  std::uint8_t* dst = dst_mask->image;
  const MaskFormat dst_format = dst_mask->format;
  const std::size_t dst_row_bytes = dst_mask->row_bytes;

  const std::size_t width = src_ft_bitmap.width;
  const std::size_t height = src_ft_bitmap.rows;

  if (MaskFormat::kLCD16 == dst_format) {
    CopyFT2LCD16<false>(src_ft_bitmap, dst_mask, false, nullptr, nullptr, nullptr);
    return;
  }

  if ((FT_PIXEL_MODE_MONO == src_format && MaskFormat::kBW == dst_format) ||
      (FT_PIXEL_MODE_GRAY == src_format && MaskFormat::kA8 == dst_format)) {
    std::size_t common_row_bytes = std::min(src_row_bytes, dst_row_bytes);
    for (std::size_t y = height; y-- > 0;) {
      std::memcpy(dst, src, common_row_bytes);
      src += src_pitch;
      dst += dst_row_bytes;
    }
  } else if (FT_PIXEL_MODE_MONO == src_format && MaskFormat::kA8 == dst_format) {
    for (std::size_t y = height; y-- > 0;) {
      std::uint8_t byte = 0;
      int bits = 0;
      const std::uint8_t* src_row = src;
      std::uint8_t* dst_row = dst;
      for (std::size_t x = width; x-- > 0;) {
        if (0 == bits) {
          byte = *src_row++;
          bits = 8;
        }
        *dst_row++ = byte & 0x80 ? 0xff : 0x00;
        bits--;
        byte = static_cast<std::uint8_t>(byte << 1);
      }
      src += src_pitch;
      dst += dst_row_bytes;
    }
  } else if (FT_PIXEL_MODE_BGRA == src_format && MaskFormat::kARGB32 == dst_format) {
    // FT_PIXEL_MODE_BGRA is pre-multiplied.
    for (std::size_t y = height; y-- > 0;) {
      const std::uint8_t* src_row = src;
      PMColor* dst_row = reinterpret_cast<PMColor*>(dst);
      for (std::size_t x = 0; x < width; ++x) {
        std::uint8_t b = *src_row++;
        std::uint8_t g = *src_row++;
        std::uint8_t r = *src_row++;
        std::uint8_t a = *src_row++;
        *dst_row++ = PackARGB32(a, r, g, b);
      }
      src += src_pitch;
      dst += dst_row_bytes;
    }
  } else {
    // unsupported combination of FT_Pixel_Mode and MaskFormat
  }
}

int Convert8To1(unsigned byte) {
  // Arbitrary decision that making the cutoff at 1/4 instead of 1/2 in
  // general looks better.
  return (byte >> 6) != 0;
}

std::uint8_t Pack8To1(const std::uint8_t alpha[8]) {
  unsigned bits = 0;
  for (int i = 0; i < 8; ++i) {
    bits <<= 1;
    bits |= static_cast<unsigned>(Convert8To1(alpha[i]));
  }
  return static_cast<std::uint8_t>(bits);
}

void PackA8ToA1(MaskBuilder* dst_mask, const std::uint8_t* src, std::size_t src_rb) {
  const int height = dst_mask->bounds.Height();
  const int width = dst_mask->bounds.Width();
  const int octs = width >> 3;
  const int left_over_bits = width & 7;

  std::uint8_t* dst = dst_mask->image;
  // SkAlign8(width) / 8.
  const int dst_pad = static_cast<int>(dst_mask->row_bytes) - ((width + 7) & ~7) / 8;

  const int src_pad = static_cast<int>(src_rb) - width;

  for (int y = 0; y < height; ++y) {
    for (int i = 0; i < octs; ++i) {
      *dst++ = Pack8To1(src);
      src += 8;
    }
    if (left_over_bits > 0) {
      unsigned bits = 0;
      int shift = 7;
      for (int i = 0; i < left_over_bits; ++i, --shift) {
        bits |= static_cast<unsigned>(Convert8To1(*src++)) << shift;
      }
      *dst++ = static_cast<std::uint8_t>(bits);
    }
    src += src_pad;
    dst += dst_pad;
  }
}

// SkMaskFormat_for_SkColorType.
MaskFormat MaskFormatForColorType(ColorType color_type) {
  switch (color_type) {
  case ColorType::kAlpha8:
    return MaskFormat::kA8;
  case ColorType::kN32:
    return MaskFormat::kARGB32;
  }
  return MaskFormat::kA8;
}

// SkColorType_for_FTPixelMode.
ColorType ColorTypeForFTPixelMode(FT_Pixel_Mode pixel_mode) {
  switch (pixel_mode) {
  case FT_PIXEL_MODE_MONO:
  case FT_PIXEL_MODE_GRAY:
    return ColorType::kAlpha8;
  case FT_PIXEL_MODE_BGRA:
    return ColorType::kN32;
  default:
    // unsupported FT_PIXEL_MODE
    return ColorType::kAlpha8;
  }
}

// SkColorType_for_SkMaskFormat.
ColorType ColorTypeForMaskFormat(MaskFormat format) {
  switch (format) {
  case MaskFormat::kBW:
  case MaskFormat::kA8:
  case MaskFormat::kLCD16:
    return ColorType::kAlpha8;
  case MaskFormat::kARGB32:
    return ColorType::kN32;
  default:
    // unsupported destination bitmap config
    return ColorType::kAlpha8;
  }
}

bool GenerateFacePathStatic(FT_Face face, std::uint16_t glyph_id,
                            ScalerContextFTUtils::LoadGlyphFlags load_flags, ScalarPath* path) {
  load_flags |= FT_LOAD_BITMAP_METRICS_ONLY; // Don't decode any bitmaps.
  load_flags |= FT_LOAD_NO_BITMAP;           // Ignore embedded bitmaps.
  load_flags &= ~static_cast<ScalerContextFTUtils::LoadGlyphFlags>(FT_LOAD_RENDER); // Don't scan convert.
  load_flags &= ~static_cast<ScalerContextFTUtils::LoadGlyphFlags>(FT_LOAD_COLOR);  // Ignore SVG.
  if (FT_Load_Glyph(face, glyph_id, static_cast<FT_Int32>(load_flags))) {
    return false;
  }
  return ScalerContextFTUtils::GenerateGlyphPath(face, path);
}

} // namespace

void ScalerContextFTUtils::Init(ColorARGB fg_color, ScalerContext::Flags context_flags) {
  foreground_color = fg_color;
  flags = context_flags;
}

bool ScalerContextFTUtils::DrawCOLRv1Glyph(FT_Face face, const PlatformGlyph& glyph, LoadGlyphFlags,
                                           std::span<ColorARGB> palette, Canvas* canvas) const {
#ifdef TT_SUPPORT_COLRV1
  if (IsSubpixel()) {
    canvas->Translate(FixedToFloat(glyph.GetSubXFixed()), FixedToFloat(glyph.GetSubYFixed()));
  }

  ActivePaints active_paints;
  return Colrv1StartGlyph(canvas, palette, foreground_color, face, glyph.GetGlyphID(),
                          FT_COLOR_INCLUDE_ROOT_TRANSFORM, &active_paints);
#else
  return false;
#endif // TT_SUPPORT_COLRV1
}

bool ScalerContextFTUtils::DrawCOLRv0Glyph(FT_Face face, const PlatformGlyph& glyph, LoadGlyphFlags load_flags,
                                           std::span<ColorARGB> palette, Canvas* canvas) const {
#ifdef FT_COLOR_H
  if (IsSubpixel()) {
    canvas->Translate(FixedToFloat(glyph.GetSubXFixed()), FixedToFloat(glyph.GetSubYFixed()));
  }

  bool have_layers = false;
  FT_LayerIterator layer_iterator;
  layer_iterator.p = nullptr;
  FT_UInt layer_glyph_index = 0;
  FT_UInt layer_color_index = 0;
  PlatformPaint paint;
  paint.SetAntiAlias(!(load_flags & FT_LOAD_TARGET_MONO));
  while (FT_Get_Color_Glyph_Layer(face, glyph.GetGlyphID(), &layer_glyph_index, &layer_color_index, &layer_iterator)) {
    have_layers = true;
    if (layer_color_index == 0xFFFF) {
      paint.SetColor(foreground_color);
    } else if (layer_color_index < palette.size()) {
      paint.SetColor(palette[layer_color_index]);
    } else {
      // Upstream reads past the palette here; an out of range layer is
      // transparent instead.
      paint.SetColor(kColorTransparent);
    }
    ScalarPath path;
    if (GenerateFacePath(face, static_cast<std::uint16_t>(layer_glyph_index), load_flags, &path)) {
      canvas->DrawPath(path, paint);
    }
  }
  return have_layers;
#else
  return false;
#endif // FT_COLOR_H
}

bool ScalerContextFTUtils::DrawSVGGlyph(FT_Face face, const PlatformGlyph& glyph, LoadGlyphFlags,
                                        std::span<ColorARGB> palette, Canvas* canvas) const {
#if defined(FT_CONFIG_OPTION_SVG)
  FT_SVG_Document ft_svg = reinterpret_cast<FT_SVG_Document>(face->glyph->other);
  FT_Matrix ft_matrix = ft_svg->transform;
  FT_Vector ft_offset = ft_svg->delta;
  ScalarMatrix m = ScalarMatrix::MakeAll(FixedToFloat(ft_matrix.xx), -FixedToFloat(ft_matrix.xy), FixedToFloat(ft_offset.x),
                                         -FixedToFloat(ft_matrix.yx), FixedToFloat(ft_matrix.yy), -FixedToFloat(ft_offset.y));
  m.PostScale(FixedToFloat(ft_svg->metrics.x_scale) / 64.0f, FixedToFloat(ft_svg->metrics.y_scale) / 64.0f);
  if (IsSubpixel()) {
    m.PostTranslate(FixedToFloat(glyph.GetSubXFixed()), FixedToFloat(glyph.GetSubYFixed()));
  }
  canvas->Concat(m);

  OpenTypeSVGDecoderFactory svg_factory = GetOpenTypeSVGDecoderFactory();
  if (!svg_factory) {
    return false;
  }
  auto svg_decoder = svg_factory(ft_svg->svg_document, ft_svg->svg_document_length);
  if (!svg_decoder) {
    return false;
  }
  return svg_decoder->Render(*canvas, ft_svg->units_per_EM, glyph.GetGlyphID(), foreground_color, palette);
#else
  return false;
#endif // FT_CONFIG_OPTION_SVG
}

bool ScalerContextFTUtils::GenerateFacePath(FT_Face face, std::uint16_t glyph_id, LoadGlyphFlags load_flags, ScalarPath* path) const {
  return GenerateFacePathStatic(face, glyph_id, load_flags, path);
}

void ScalerContextFTUtils::GenerateGlyphImage(FT_Face face, const PlatformGlyph& glyph, void* image_buffer,
                                              const ScalarMatrix& bitmap_transform,
                                              const MaskGamma::PreBlend& pre_blend) const {
  switch (face->glyph->format) {
  case FT_GLYPH_FORMAT_OUTLINE: {
    FT_Outline* outline = &face->glyph->outline;

    int dx = 0, dy = 0;
    if (IsSubpixel()) {
      // SkFixedToFDot6.
      dx = glyph.GetSubXFixed() >> 10;
      dy = glyph.GetSubYFixed() >> 10;
      // negate dy since freetype-y-goes-up and skia-y-goes-down
      dy = -dy;
    }

    std::memset(image_buffer, 0, glyph.RowBytes() * static_cast<std::size_t>(glyph.Height()));

    if (MaskFormat::kLCD16 == glyph.GetMaskFormat()) {
      const bool do_bgr = (flags & ScalerContext::kLCD_BGROrder_Flag) != 0;
      const bool do_vert = (flags & ScalerContext::kLCD_Vertical_Flag) != 0;

      FT_Outline_Translate(outline, dx, dy);
      FT_Error err = FT_Render_Glyph(face->glyph, do_vert ? FT_RENDER_MODE_LCD_V : FT_RENDER_MODE_LCD);
      if (err) {
        return;
      }

      MaskBuilder mask(static_cast<std::uint8_t*>(image_buffer),
                       glyph.IRect(), static_cast<std::uint32_t>(glyph.RowBytes()), glyph.GetMaskFormat());

      FT_GlyphSlotRec& ft_glyph = *face->glyph;

      if (!IntRect::Intersects(mask.bounds,
                               IntRect::MakeXYWH(ft_glyph.bitmap_left,
                                                 -ft_glyph.bitmap_top,
                                                 static_cast<std::int32_t>(ft_glyph.bitmap.width),
                                                 static_cast<std::int32_t>(ft_glyph.bitmap.rows)))) {
        return;
      }

      // If the FT_Bitmap extent is larger, discard bits of the bitmap outside
      // the mask. If the mask extent is larger, shrink mask to fit bitmap
      // (clearing discarded).
      unsigned char* orig_buffer = ft_glyph.bitmap.buffer;
      // First align the top left (origin).
      if (-ft_glyph.bitmap_top < mask.bounds.top) {
        std::int32_t top_diff = mask.bounds.top - (-ft_glyph.bitmap_top);
        ft_glyph.bitmap.buffer += ft_glyph.bitmap.pitch * top_diff;
        ft_glyph.bitmap.rows -= static_cast<unsigned int>(top_diff);
        ft_glyph.bitmap_top = -mask.bounds.top;
      }
      if (ft_glyph.bitmap_left < mask.bounds.left) {
        std::int32_t left_diff = mask.bounds.left - ft_glyph.bitmap_left;
        ft_glyph.bitmap.buffer += left_diff;
        ft_glyph.bitmap.width -= static_cast<unsigned int>(left_diff);
        ft_glyph.bitmap_left = mask.bounds.left;
      }
      if (mask.bounds.top < -ft_glyph.bitmap_top) {
        mask.image += mask.row_bytes * static_cast<std::uint32_t>(-ft_glyph.bitmap_top - mask.bounds.top);
        mask.bounds.top = -ft_glyph.bitmap_top;
      }
      if (mask.bounds.left < ft_glyph.bitmap_left) {
        mask.image += sizeof(std::uint16_t) * static_cast<std::size_t>(ft_glyph.bitmap_left - mask.bounds.left);
        mask.bounds.left = ft_glyph.bitmap_left;
      }
      // Origins aligned, clean up the width and height.
      int ft_vert_scale = (do_vert ? 3 : 1);
      int ft_hori_scale = (do_vert ? 1 : 3);
      if (mask.bounds.Height() * ft_vert_scale < static_cast<int>(ft_glyph.bitmap.rows)) {
        ft_glyph.bitmap.rows = static_cast<unsigned int>(mask.bounds.Height() * ft_vert_scale);
      }
      if (mask.bounds.Width() * ft_hori_scale < static_cast<int>(ft_glyph.bitmap.width)) {
        ft_glyph.bitmap.width = static_cast<unsigned int>(mask.bounds.Width() * ft_hori_scale);
      }
      if (static_cast<int>(ft_glyph.bitmap.rows) < mask.bounds.Height() * ft_vert_scale) {
        mask.bounds.bottom = mask.bounds.top + static_cast<std::int32_t>(ft_glyph.bitmap.rows) / ft_vert_scale;
      }
      if (static_cast<int>(ft_glyph.bitmap.width) < mask.bounds.Width() * ft_hori_scale) {
        mask.bounds.right = mask.bounds.left + static_cast<std::int32_t>(ft_glyph.bitmap.width) / ft_hori_scale;
      }
      if (pre_blend.IsApplicable()) {
        CopyFT2LCD16<true>(ft_glyph.bitmap, &mask, do_bgr, pre_blend.r, pre_blend.g, pre_blend.b);
      } else {
        CopyFT2LCD16<false>(ft_glyph.bitmap, &mask, do_bgr, pre_blend.r, pre_blend.g, pre_blend.b);
      }
      // Restore the buffer pointer so FreeType can properly free it.
      ft_glyph.bitmap.buffer = orig_buffer;
    } else {
      FT_BBox bbox;
      FT_Bitmap target;
      FT_Outline_Get_CBox(outline, &bbox);
      // what we really want to do for subpixel is
      //   offset(dx, dy)
      //   compute_bounds
      //   offset(bbox & !63)
      // but that is two calls to offset, so we do the following, which
      // achieves the same thing with only one offset call.
      FT_Outline_Translate(outline, dx - ((bbox.xMin + dx) & ~63),
                           dy - ((bbox.yMin + dy) & ~63));

      target.width = static_cast<unsigned int>(glyph.Width());
      target.rows = static_cast<unsigned int>(glyph.Height());
      target.pitch = static_cast<int>(glyph.RowBytes());
      target.buffer = static_cast<std::uint8_t*>(image_buffer);
      target.pixel_mode = static_cast<unsigned char>(ComputePixelMode(glyph.GetMaskFormat()));
      target.num_grays = 256;

      FT_Outline_Get_Bitmap(face->glyph->library, outline, &target);
    }
  } break;

  case FT_GLYPH_FORMAT_BITMAP: {
    // If no scaling needed, directly copy glyph bitmap.
    if (bitmap_transform.IsIdentity()) {
      MaskBuilder dst_mask = MaskBuilder(static_cast<std::uint8_t*>(image_buffer),
                                         glyph.IRect(), static_cast<std::uint32_t>(glyph.RowBytes()),
                                         glyph.GetMaskFormat());
      CopyFTBitmap(face->glyph->bitmap, &dst_mask);
      break;
    }

    // Otherwise, scale the bitmap.
    const MaskFormat mask_format = glyph.GetMaskFormat();
    const FT_Pixel_Mode pixel_mode = static_cast<FT_Pixel_Mode>(face->glyph->bitmap.pixel_mode);

    // Copy the FT_Bitmap into a bitmap (either A8 or ARGB)
    const ColorType unscaled_color_type = ColorTypeForFTPixelMode(pixel_mode);
    Bitmap unscaled_bitmap(unscaled_color_type,
                           static_cast<int>(face->glyph->bitmap.width),
                           static_cast<int>(face->glyph->bitmap.rows));
    if (unscaled_bitmap.IsEmpty()) {
      // TODO: set the image buffer to indicate "missing"
      std::memset(image_buffer, 0, glyph.RowBytes() * static_cast<std::size_t>(glyph.Height()));
      return;
    }

    const Pixmap& unscaled_pixmap = unscaled_bitmap.GetPixmap();
    MaskBuilder unscaled_bitmap_alias(static_cast<std::uint8_t*>(unscaled_pixmap.WritableAddr()),
                                      IntRect::MakeWH(unscaled_pixmap.Width(), unscaled_pixmap.Height()),
                                      static_cast<std::uint32_t>(unscaled_pixmap.RowBytes()),
                                      MaskFormatForColorType(unscaled_color_type));
    CopyFTBitmap(face->glyph->bitmap, &unscaled_bitmap_alias);
    auto unscaled_image = std::make_shared<const Image>(std::move(unscaled_bitmap));

    // Wrap the glyph's mask in a bitmap, unless the glyph's mask is BW or
    // LCD. BW requires an A8 target for resizing, which can then be down
    // sampled. LCD should use a 4x A8 target, which will then be down
    // sampled. For simplicity, LCD uses A8 and is replicated.
    Bitmap dst_storage;
    Pixmap dst_pixmap;
    const ColorType dst_color_type = ColorTypeForMaskFormat(mask_format);
    if (MaskFormat::kBW == mask_format || MaskFormat::kLCD16 == mask_format) {
      dst_storage = Bitmap(dst_color_type, glyph.Width(), glyph.Height());
      if (dst_storage.IsEmpty()) {
        // TODO: set the image to indicate "missing"
        std::memset(image_buffer, 0, glyph.RowBytes() * static_cast<std::size_t>(glyph.Height()));
        return;
      }
      dst_pixmap = dst_storage.GetPixmap();
    } else {
      dst_pixmap = Pixmap(dst_color_type, glyph.Width(), glyph.Height(), image_buffer, glyph.RowBytes());
    }

    // Scale unscaled_image into dst_pixmap.
    {
      RasterCanvas canvas(dst_pixmap);
      canvas.Clear(kColorTransparent);
      canvas.Translate(static_cast<float>(-glyph.Left()), static_cast<float>(-glyph.Top()));
      canvas.Concat(bitmap_transform);
      canvas.Translate(static_cast<float>(face->glyph->bitmap_left), static_cast<float>(-face->glyph->bitmap_top));

      SamplingOptions sampling{FilterMode::kLinear, MipmapMode::kNearest};
      canvas.DrawImage(unscaled_image, 0, 0, sampling, nullptr);
    }

    // If the destination is BW or LCD, convert from A8.
    if (MaskFormat::kBW == mask_format) {
      // Copy the A8 dst into the A1 image buffer.
      MaskBuilder dst_mask(static_cast<std::uint8_t*>(image_buffer),
                           glyph.IRect(), static_cast<std::uint32_t>(glyph.RowBytes()), glyph.GetMaskFormat());
      PackA8ToA1(&dst_mask, static_cast<const std::uint8_t*>(dst_pixmap.Addr()), dst_pixmap.RowBytes());
    } else if (MaskFormat::kLCD16 == mask_format) {
      // Copy the A8 dst into the LCD16 image buffer.
      const std::uint8_t* src = static_cast<const std::uint8_t*>(dst_pixmap.Addr());
      std::uint16_t* dst = reinterpret_cast<std::uint16_t*>(image_buffer);
      for (int y = dst_pixmap.Height(); y-- > 0;) {
        for (int x = 0; x < dst_pixmap.Width(); ++x) {
          dst[x] = GrayToRGB16(src[x]);
        }
        dst = reinterpret_cast<std::uint16_t*>(reinterpret_cast<char*>(dst) + glyph.RowBytes());
        src += dst_pixmap.RowBytes();
      }
    }
  } break;

  default:
    // unknown glyph format
    std::memset(image_buffer, 0, glyph.RowBytes() * static_cast<std::size_t>(glyph.Height()));
    return;
  }

  // SK_GAMMA_APPLY_TO_A8 is not defined, so the pre-blend is not applied to
  // A8 masks here.
}

bool ScalerContextFTUtils::GenerateGlyphPath(FT_Face face, ScalarPath* path) {
  FTGeometrySink sink(path);
  if (face->glyph->format != FT_GLYPH_FORMAT_OUTLINE ||
      FT_Outline_Decompose(&face->glyph->outline, &FTGeometrySink::kFuncs, &sink)) {
    return false;
  }
  path->Close();
  return true;
}

bool ScalerContextFTUtils::ComputeColrV1GlyphBoundingBox(FT_Face face, std::uint16_t glyph_id, ScalarRect* bounds) {
  *bounds = ScalarRect();
#ifdef TT_SUPPORT_COLRV1
  ActivePaints active_paints;
  return StartGlyphBounds(ScalarMatrix(), bounds, face, glyph_id, FT_COLOR_INCLUDE_ROOT_TRANSFORM, &active_paints);
#else
  return false;
#endif
}

} // namespace bkit
