// Ported from: skia/src/core/SkGlyph.cpp

#include "platform_glyph.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <tuple>
#include <utility>

#include "arena.h"
#include "base/immediate_crash.h"
#include "paint/bezier_curves.h"
#include "paint/path_geometry.h"
#include "paint/picture.h"
#include "paint/scalar.h"
#include "scaler_context.h"
#include "strike.h"

namespace bkfont {

namespace {

std::size_t BitsToBytes(std::size_t bits) {
  return (bits + 7) >> 3;
}

std::size_t FormatAlignmentOf(MaskFormat format) {
  switch (format) {
  case MaskFormat::kBW:
  case MaskFormat::kA8:
  case MaskFormat::k3D:
  case MaskFormat::kSDF:
    return alignof(std::uint8_t);
  case MaskFormat::kARGB32:
    return alignof(std::uint32_t);
  case MaskFormat::kLCD16:
    return alignof(std::uint16_t);
  }
  // SK_ABORT("Unknown mask format.").
  base::ImmediateCrash();
}

std::size_t FormatRowBytes(int width, MaskFormat format) {
  return format == MaskFormat::kBW ? BitsToBytes(static_cast<std::size_t>(width)) : static_cast<std::size_t>(width) * FormatAlignmentOf(format);
}

std::uint32_t InitActions(const PlatformGlyph& glyph) {
  constexpr std::uint32_t kAllUnset = 0;
  constexpr std::uint32_t kDrop = static_cast<std::uint32_t>(GlyphAction::kDrop);
  constexpr std::uint32_t kAllDrop = kDrop << kDirectMask |
                                     kDrop << kDirectMaskCPU |
                                     kDrop << kMask |
                                     kDrop << kSDFT |
                                     kDrop << kPath |
                                     kDrop << kDrawable;
  return glyph.IsEmpty() ? kAllDrop : kAllUnset;
}

} // namespace

// -- PlatformGlyph ------------------------------------------------------------

Mask PlatformGlyph::GetMask() const {
  IntRect bounds = IntRect::MakeXYWH(left_, top_, width_, height_);
  return Mask(static_cast<const std::uint8_t*>(image_), bounds, static_cast<std::uint32_t>(RowBytes()), mask_format_);
}

Mask PlatformGlyph::GetMask(ScalarPoint position) const {
  IntRect bounds = IntRect::MakeXYWH(left_, top_, width_, height_);
  // SkScalarFloorToInt.
  bounds.Offset(FloatSaturateToInt(std::floor(position.x)), FloatSaturateToInt(std::floor(position.y)));
  return Mask(static_cast<const std::uint8_t*>(image_), bounds, static_cast<std::uint32_t>(RowBytes()), mask_format_);
}

void PlatformGlyph::ZeroMetrics() {
  advance_x_ = 0;
  advance_y_ = 0;
  width_ = 0;
  height_ = 0;
  top_ = 0;
  left_ = 0;
}

std::size_t PlatformGlyph::FormatAlignment() const {
  return FormatAlignmentOf(GetMaskFormat());
}

std::size_t PlatformGlyph::AllocImage(Arena* arena) {
  const std::size_t size = ImageSize();
  // The arena's blocks satisfy the strictest format alignment, uint32_t.
  image_ = arena->MakeBytes(size);
  return size;
}

bool PlatformGlyph::SetImage(Arena* arena, ScalerContext* scaler_context) {
  if (!SetImageHasBeenCalled()) {
    // It used to be that GetImage() could change the mask format. The debug
    // check that it does not is not ported.
    AllocImage(arena);
    scaler_context->GetImage(*this);
    return true;
  }
  return false;
}

bool PlatformGlyph::SetImage(Arena* arena, const void* image) {
  if (!SetImageHasBeenCalled()) {
    AllocImage(arena);
    std::memcpy(image_, image, ImageSize());
    return true;
  }
  return false;
}

std::size_t PlatformGlyph::RowBytes() const {
  return FormatRowBytes(width_, mask_format_);
}

std::size_t PlatformGlyph::RowBytesUsingFormat(MaskFormat format) const {
  return FormatRowBytes(width_, format);
}

std::size_t PlatformGlyph::ImageSize() const {
  if (IsEmpty() || ImageTooLarge()) {
    return 0;
  }

  std::size_t size = RowBytes() * height_;

  if (mask_format_ == MaskFormat::k3D) {
    size *= 3;
  }

  return size;
}

void PlatformGlyph::InstallPath(Arena* arena, const ScalarPath* path, bool hairline, bool modified) {
  path_data_ = arena->Make<PathData>();
  if (path != nullptr) {
    path_data_->path = *path;
    path_data_->has_path = true;
    path_data_->hairline = hairline;
    path_data_->modified = modified;
  }
}

bool PlatformGlyph::SetPath(Arena* arena, ScalerContext* scaler_context) {
  if (!SetPathHasBeenCalled()) {
    scaler_context->GetPath(*this, arena);
    return Path() != nullptr;
  }

  return false;
}

bool PlatformGlyph::SetPath(Arena* arena, const ScalarPath* path, bool hairline, bool modified) {
  if (!SetPathHasBeenCalled()) {
    InstallPath(arena, path, hairline, modified);
    return Path() != nullptr;
  }
  return false;
}

const ScalarPath* PlatformGlyph::Path() const {
  // SetPath must have been called previously.
  if (path_data_->has_path) {
    return &path_data_->path;
  }
  return nullptr;
}

bool PlatformGlyph::PathIsHairline() const {
  // SetPath must have been called previously.
  return path_data_->hairline;
}

bool PlatformGlyph::PathIsModified() const {
  // SetPath must have been called previously.
  return path_data_->modified;
}

void PlatformGlyph::InstallDrawable(Arena* arena, std::shared_ptr<Drawable> drawable) {
  drawable_data_ = arena->Make<DrawableData>();
  if (drawable != nullptr) {
    drawable_data_->drawable = std::move(drawable);
    drawable_data_->has_drawable = true;
  }
}

bool PlatformGlyph::SetDrawable(Arena* arena, ScalerContext* scaler_context) {
  if (!SetDrawableHasBeenCalled()) {
    std::shared_ptr<Drawable> drawable = scaler_context->GetDrawable(*this);
    InstallDrawable(arena, std::move(drawable));
    return GetDrawable() != nullptr;
  }
  return false;
}

bool PlatformGlyph::SetDrawable(Arena* arena, std::shared_ptr<Drawable> drawable) {
  if (!SetDrawableHasBeenCalled()) {
    InstallDrawable(arena, std::move(drawable));
    return GetDrawable() != nullptr;
  }
  return false;
}

Drawable* PlatformGlyph::GetDrawable() const {
  // SetDrawable must have been called previously.
  if (drawable_data_->has_drawable) {
    return drawable_data_->drawable.get();
  }
  return nullptr;
}

namespace {

std::tuple<float, float> CalculatePathGap(float top_offset, float bottom_offset, const ScalarPath& path) {
  // Left and Right of an ever expanding gap around the path.
  float left = std::numeric_limits<float>::max();
  // SK_ScalarMin.
  float right = -std::numeric_limits<float>::max();

  auto expand_gap = [&left, &right](float v) {
    left = std::min(left, v);
    right = std::max(right, v);
  };

  // Handle all the different verbs for the path.
  auto add_line = [&](std::span<const ScalarPoint> pts, float offset) {
    // sk_ieee_float_divide.
    float t = (offset - pts[0].y) / (pts[1].y - pts[0].y);
    if (0 <= t && t < 1) { // this handles divide by zero above
      expand_gap(pts[0].x + t * (pts[1].x - pts[0].x));
    }
  };

  auto add_quad = [&](std::span<const ScalarPoint> pts, float offset) {
    float intersection_storage[2];
    auto intersections = BezierQuad::IntersectWithHorizontalLine(pts, offset, intersection_storage);
    for (float x : intersections) {
      expand_gap(x);
    }
  };

  auto add_cubic = [&](std::span<const ScalarPoint> pts, float offset) {
    float intersection_storage[3];
    auto intersections = BezierCubic::IntersectWithHorizontalLine(pts, offset, intersection_storage);
    for (float intersection : intersections) {
      expand_gap(intersection);
    }
  };

  // Handle when a verb's points are in the gap between top and bottom.
  auto add_pts = [&expand_gap, top_offset, bottom_offset](std::span<const ScalarPoint> pts) {
    for (const ScalarPoint p : pts) {
      if (top_offset < p.y && p.y < bottom_offset) {
        expand_gap(p.x);
      }
    }
  };

  auto handle_line = [&](std::span<const ScalarPoint> pts) {
    auto [line_top, line_bottom] = std::minmax({pts[0].y, pts[1].y});

    // The y-coordinates of the points intersect the top and bottom offsets.
    if (top_offset <= line_bottom && line_top <= bottom_offset) {
      add_line(pts, top_offset);
      add_line(pts, bottom_offset);
      add_pts(pts);
    }
  };

  // SkPath::Iter without forced closing: an explicit close emits the closing
  // line when the last point is not the move point.
  const auto points = path.Points();
  std::size_t index = 0;
  std::size_t conic_index = 0;
  ScalarPoint move_to;
  ScalarPoint last;
  for (ScalarPath::Verb verb : path.Verbs()) {
    switch (verb) {
    case ScalarPath::Verb::kMove: {
      move_to = last = points[index++];
      break;
    }
    case ScalarPath::Verb::kLine: {
      const ScalarPoint pts[2] = {last, points[index]};
      index += 1;
      handle_line(pts);
      last = pts[1];
      break;
    }
    case ScalarPath::Verb::kQuad: {
      const ScalarPoint pts[3] = {last, points[index], points[index + 1]};
      index += 2;
      auto [quad_top, quad_bottom] = std::minmax({pts[0].y, pts[1].y, pts[2].y});

      // The y-coordinates of the points intersect the top and bottom offsets.
      if (top_offset <= quad_bottom && quad_top <= bottom_offset) {
        add_quad(pts, top_offset);
        add_quad(pts, bottom_offset);
        add_pts(pts);
      }
      last = pts[2];
      break;
    }
    case ScalarPath::Verb::kConic: {
      const ScalarPoint pts[3] = {last, points[index], points[index + 1]};
      index += 2;
      const float w = path.ConicWeights()[conic_index++];
      const auto [top, bottom] = std::minmax({pts[0].y, pts[1].y, pts[2].y});
      if (top_offset <= bottom && top <= bottom_offset) {
        for (float y : {top_offset, bottom_offset}) {
          float roots[2];
          const float p0 = pts[0].y - y;
          const float p1 = w * (pts[1].y - y);
          const float p2 = pts[2].y - y;
          const int count = FindUnitQuadRoots(p0 - 2 * p1 + p2, 2 * (p1 - p0), p0, roots);
          for (int i = 0; i < count; ++i) {
            const float t = roots[i], s = 1 - t;
            expand_gap((s * s * pts[0].x + 2 * w * s * t * pts[1].x + t * t * pts[2].x) /
                       (s * s + 2 * w * s * t + t * t));
          }
        }
        add_pts(pts);
      }
      last = pts[2];
      break;
    }
    case ScalarPath::Verb::kCubic: {
      const ScalarPoint pts[4] = {last, points[index], points[index + 1], points[index + 2]};
      index += 3;
      auto [cubic_top, cubic_bottom] = std::minmax({pts[0].y, pts[1].y, pts[2].y, pts[3].y});

      // The y-coordinates of the points intersect the top and bottom offsets.
      if (top_offset <= cubic_bottom && cubic_top <= bottom_offset) {
        add_cubic(pts, top_offset);
        add_cubic(pts, bottom_offset);
        add_pts(pts);
      }
      last = pts[3];
      break;
    }
    case ScalarPath::Verb::kClose: {
      if (last.x != move_to.x || last.y != move_to.y) {
        const ScalarPoint pts[2] = {last, move_to};
        handle_line(pts);
      }
      last = move_to;
      break;
    }
    }
  }

  return std::tie(left, right);
}

} // namespace

void PlatformGlyph::EnsureIntercepts(const float bounds[2], float scale, float x_pos,
                                     float* array, int* count, Arena* arena) {
  auto offset_results = [scale, x_pos](const Intercept* intercept, float* out, int* out_count) {
    if (out) {
      out += *out_count;
      for (int index = 0; index < 2; index++) {
        *out++ = intercept->interval[index] * scale + x_pos;
      }
    }
    *out_count += 2;
  };

  const Intercept* match = [this](const float b[2]) -> const Intercept* {
    if (path_data_ == nullptr) {
      return nullptr;
    }
    const Intercept* intercept = path_data_->intercept;
    while (intercept != nullptr) {
      if (b[0] == intercept->bounds[0] && b[1] == intercept->bounds[1]) {
        return intercept;
      }
      intercept = intercept->next;
    }
    return nullptr;
  }(bounds);

  if (match != nullptr) {
    if (match->interval[0] < match->interval[1]) {
      offset_results(match, array, count);
    }
    return;
  }

  Intercept* intercept = arena->Make<Intercept>();
  intercept->next = path_data_->intercept;
  intercept->bounds[0] = bounds[0];
  intercept->bounds[1] = bounds[1];
  intercept->interval[0] = std::numeric_limits<float>::max();
  intercept->interval[1] = -std::numeric_limits<float>::max();
  path_data_->intercept = intercept;
  const ScalarPath* path = &(path_data_->path);
  const ScalarRect path_bounds = path->GetBounds();
  if (path_bounds.bottom < bounds[0] || bounds[1] < path_bounds.top) {
    return;
  }

  std::tie(intercept->interval[0], intercept->interval[1]) = CalculatePathGap(bounds[0], bounds[1], *path);

  if (intercept->interval[0] >= intercept->interval[1]) {
    intercept->interval[0] = std::numeric_limits<float>::max();
    intercept->interval[1] = -std::numeric_limits<float>::max();
    return;
  }
  offset_results(intercept, array, count);
}

// -- GlyphDigest --------------------------------------------------------------

GlyphDigest::GlyphDigest(std::size_t index, const PlatformGlyph& glyph)
    : packed_id_{glyph.GetPackedID().Value()},
      index_{static_cast<std::uint32_t>(index)},
      is_empty_(glyph.IsEmpty()),
      format_(glyph.GetMaskFormat()),
      actions_{InitActions(glyph)},
      left_{static_cast<std::int16_t>(glyph.Left())},
      top_{static_cast<std::int16_t>(glyph.Top())},
      width_{static_cast<std::uint16_t>(glyph.Width())},
      height_{static_cast<std::uint16_t>(glyph.Height())} {
}

void GlyphDigest::SetActionFor(GlyphActionType action_type, PlatformGlyph* glyph, Strike* strike) {
  // We don't have to do any more if the glyph is marked as kDrop because it
  // was IsEmpty().
  if (ActionFor(action_type) == GlyphAction::kUnset) {
    GlyphAction action = GlyphAction::kReject;
    switch (action_type) {
    case kDirectMask: {
      if (FitsInAtlasDirect()) {
        action = GlyphAction::kAccept;
      }
      break;
    }
    case kDirectMaskCPU: {
      if (strike->PrepareForImage(glyph)) {
        action = GlyphAction::kAccept;
      }
      break;
    }
    case kMask: {
      if (FitsInAtlasInterpolated()) {
        action = GlyphAction::kAccept;
      }
      break;
    }
    case kSDFT: {
      if (FitsInAtlasDirect() && GetMaskFormat() == MaskFormat::kSDF) {
        action = GlyphAction::kAccept;
      }
      break;
    }
    case kPath: {
      if (strike->PrepareForPath(glyph)) {
        action = GlyphAction::kAccept;
      }
      break;
    }
    case kDrawable: {
      if (strike->PrepareForDrawable(glyph)) {
        action = GlyphAction::kAccept;
      }
      break;
    }
    }
    SetAction(action_type, action);
  }
}

bool GlyphDigest::FitsInAtlas(const PlatformGlyph& glyph) {
  return glyph.MaxDimension() <= kSkSideTooBigForAtlas;
}

// -- GlyphPositionRoundingSpec ------------------------------------------------

namespace {

ScalarPoint HalfAxisSampleFreq(bool is_subpixel, AxisAlignment axis_alignment) {
  if (!is_subpixel) {
    return {0.5f, 0.5f};
  } else {
    switch (axis_alignment) {
    case AxisAlignment::kX:
      return {PackedGlyphID::kSubpixelRound, 0.5f};
    case AxisAlignment::kY:
      return {0.5f, PackedGlyphID::kSubpixelRound};
    case AxisAlignment::kNone:
      return {PackedGlyphID::kSubpixelRound, PackedGlyphID::kSubpixelRound};
    }
  }

  // Some compilers need this.
  return {0, 0};
}

std::int32_t IgnorePositionMaskX(bool is_subpixel, AxisAlignment axis_alignment) {
  return (!is_subpixel || axis_alignment == AxisAlignment::kY) ? 0 : ~0;
}

std::int32_t IgnorePositionMaskY(bool is_subpixel, AxisAlignment axis_alignment) {
  return (!is_subpixel || axis_alignment == AxisAlignment::kX) ? 0 : ~0;
}

} // namespace

GlyphPositionRoundingSpec::GlyphPositionRoundingSpec(bool is_subpixel, AxisAlignment axis_alignment)
    : half_axis_sample_freq{HalfAxisSampleFreq(is_subpixel, axis_alignment)},
      ignore_position_mask_x{IgnorePositionMaskX(is_subpixel, axis_alignment)},
      ignore_position_mask_y{IgnorePositionMaskY(is_subpixel, axis_alignment)},
      ignore_position_field_mask_x{IgnorePositionMaskX(is_subpixel, axis_alignment) & PackedGlyphID::kXFieldMask},
      ignore_position_field_mask_y{IgnorePositionMaskY(is_subpixel, axis_alignment) & PackedGlyphID::kYFieldMask} {
}

} // namespace bkfont
