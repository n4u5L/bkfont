// Ported from: skia/src/core/SkGlyphRunPainter.cpp
// Ported from: skia/src/core/SkPoint.cpp

#include "glyph_run_painter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

#include "canvas.h"
#include "glyph_run.h"
#include "picture.h"
#include "platform/platform_glyph.h"
#include "platform_paint.h"
#include "platform/strike.h"
#include "platform/strike_spec.h"

namespace bkfont {

namespace {

ScalerContextFlags ComputeScalerContextFlags() {
  // If we're doing linear blending, then we can disable the gamma hacks.
  // Otherwise, leave them on. In either case, we still want the contrast
  // boost. The legacy sRGB-encoded device space is never linear.
  return ScalerContextFlags::kFakeGammaAndBoostContrast;
}

// SkIsFinite(x, y).
bool IsFinite(float x, float y) {
  return std::isfinite(x) && std::isfinite(y);
}

// SkPoint::Length.
float PointLength(float dx, float dy) {
  float mag2 = dx * dx + dy * dy;
  if (std::isfinite(mag2)) {
    return std::sqrt(mag2);
  } else {
    double xx = dx;
    double yy = dy;
    return static_cast<float>(std::sqrt(xx * xx + yy * yy));
  }
}

// The SkZip<const SkGlyphID, const SkPoint> of the glyphs still to draw.
struct GlyphSource {
  std::span<const std::uint16_t> glyph_ids;
  std::span<const ScalarPoint> positions;

  bool empty() const {
    return glyph_ids.empty();
  }
};

// The SkZip buffers for accepted glyphs and rejected glyph ids. A rejected
// buffer may also hold the source being prepared: each glyph is read before
// its slot can be overwritten.
struct GlyphBuffers {
  explicit GlyphBuffers(std::size_t size)
      : accepted_glyphs(size), accepted_positions(size), rejected_glyph_ids(size), rejected_positions(size) {
  }

  std::vector<const PlatformGlyph*> accepted_glyphs;
  std::vector<ScalarPoint> accepted_positions;
  std::vector<std::uint16_t> rejected_glyph_ids;
  std::vector<ScalarPoint> rejected_positions;
};

struct PreparedGlyphs {
  std::span<const PlatformGlyph* const> accepted_glyphs;
  std::span<const ScalarPoint> accepted_positions;
  GlyphSource rejected;
};

PreparedGlyphs MakePrepared(GlyphBuffers* buffers, std::size_t accepted_size, std::size_t rejected_size) {
  return {std::span<const PlatformGlyph* const>(buffers->accepted_glyphs.data(), accepted_size),
          std::span<const ScalarPoint>(buffers->accepted_positions.data(), accepted_size),
          {std::span<const std::uint16_t>(buffers->rejected_glyph_ids.data(), rejected_size),
           std::span<const ScalarPoint>(buffers->rejected_positions.data(), rejected_size)}};
}

// prepare_for_path_drawing and prepare_for_drawable_drawing, which differ
// only in the action type.
PreparedGlyphs PrepareForSourceSpaceDrawing(Strike* strike,
                                            GlyphActionType action_type,
                                            GlyphSource source,
                                            GlyphBuffers* buffers) {
  std::size_t accepted_size = 0;
  std::size_t rejected_size = 0;
  strike->Lock();
  for (std::size_t i = 0; i < source.glyph_ids.size(); ++i) {
    const std::uint16_t glyph_id = source.glyph_ids[i];
    const ScalarPoint pos = source.positions[i];
    if (!IsFinite(pos.x, pos.y)) {
      continue;
    }
    const PackedGlyphID packed_id{glyph_id};
    GlyphDigest digest = strike->DigestFor(action_type, packed_id);
    switch (digest.ActionFor(action_type)) {
    case GlyphAction::kAccept:
      buffers->accepted_glyphs[accepted_size] = strike->Glyph(digest);
      buffers->accepted_positions[accepted_size] = pos;
      ++accepted_size;
      break;
    case GlyphAction::kReject:
      buffers->rejected_glyph_ids[rejected_size] = glyph_id;
      buffers->rejected_positions[rejected_size] = pos;
      ++rejected_size;
      break;
    default:
      break;
    }
  }
  strike->Unlock();
  return MakePrepared(buffers, accepted_size, rejected_size);
}

// prepare_for_direct_mask_drawing, and with map_accepted false,
// prepare_for_direct_bitmap_drawing, whose accepted points are unmapped
// source points.
PreparedGlyphs PrepareForDirectDrawing(Strike* strike,
                                       const ScalarMatrix& creation_matrix,
                                       GlyphSource source,
                                       GlyphBuffers* buffers,
                                       bool map_accepted) {
  const GlyphPositionRoundingSpec& rounding_spec = strike->RoundingSpec();
  const std::int32_t mask_x = rounding_spec.ignore_position_field_mask_x;
  const std::int32_t mask_y = rounding_spec.ignore_position_field_mask_y;
  const ScalarPoint half_sample_freq = rounding_spec.half_axis_sample_freq;

  // Build up the mapping from source space to device space. Add the rounding
  // constant halfSampleFreq, so we just need to floor to get the device
  // result.
  ScalarMatrix position_matrix_with_rounding = creation_matrix;
  position_matrix_with_rounding.PostTranslate(half_sample_freq.x, half_sample_freq.y);

  std::size_t accepted_size = 0;
  std::size_t rejected_size = 0;
  strike->Lock();
  for (std::size_t i = 0; i < source.glyph_ids.size(); ++i) {
    const std::uint16_t glyph_id = source.glyph_ids[i];
    const ScalarPoint pos = source.positions[i];
    if (!IsFinite(pos.x, pos.y)) {
      continue;
    }

    const ScalarPoint mapped_pos = position_matrix_with_rounding.MapPoint(pos);
    const PackedGlyphID packed_glyph_id{glyph_id, mapped_pos, mask_x, mask_y};
    GlyphDigest digest = strike->DigestFor(kDirectMaskCPU, packed_glyph_id);
    switch (digest.ActionFor(kDirectMaskCPU)) {
    case GlyphAction::kAccept: {
      buffers->accepted_glyphs[accepted_size] = strike->Glyph(digest);
      if (map_accepted) {
        const ScalarPoint rounded_pos{std::floor(mapped_pos.x), std::floor(mapped_pos.y)};
        buffers->accepted_positions[accepted_size] = rounded_pos;
      } else {
        buffers->accepted_positions[accepted_size] = pos;
      }
      ++accepted_size;
      break;
    }
    case GlyphAction::kReject:
      buffers->rejected_glyph_ids[rejected_size] = glyph_id;
      buffers->rejected_positions[rejected_size] = pos;
      ++rejected_size;
      break;
    default:
      break;
    }
  }
  strike->Unlock();

  return MakePrepared(buffers, accepted_size, rejected_size);
}

// SkMatrix::setScaleTranslate.
ScalarMatrix MakeScaleTranslate(float sx, float sy, float tx, float ty) {
  return ScalarMatrix::MakeAll(sx, 0, tx, 0, sy, ty);
}

} // namespace

std::shared_ptr<const Image> MakeImageFromARGB32Mask(const Mask& mask) {
  Bitmap bitmap(ColorType::kN32, mask.bounds.Width(), mask.bounds.Height());
  if (bitmap.IsEmpty() || mask.image == nullptr) {
    return nullptr;
  }
  const Pixmap& pixmap = bitmap.GetPixmap();
  const std::size_t row_size = static_cast<std::size_t>(pixmap.Width()) * BytesPerPixel(ColorType::kN32);
  for (int y = 0; y < pixmap.Height(); ++y) {
    std::memcpy(pixmap.WritableAddr8(0, y), mask.image + static_cast<std::size_t>(y) * mask.row_bytes, row_size);
  }
  return std::make_shared<const Image>(std::move(bitmap));
}

// -- GlyphRunListPainterCPU ---------------------------------------------------

GlyphRunListPainterCPU::GlyphRunListPainterCPU(const SurfaceProps& props, ColorType color_type)
    : device_props_{props},
      bitmap_fallback_props_{props.CloneWithPixelGeometry(PixelGeometry::kUnknown)},
      color_type_{color_type},
      scaler_context_flags_{ComputeScalerContextFlags()} {
}

void GlyphRunListPainterCPU::DrawForBitmapDevice(Canvas* canvas, BitmapDevicePainter* bitmap_device,
                                                 const GlyphRunList& glyph_run_list, const PlatformPaint& paint,
                                                 const ScalarMatrix& draw_matrix) {
  const std::size_t max_glyph_run_size = glyph_run_list.MaxGlyphRunSize();
  GlyphBuffers buffers(max_glyph_run_size);

  // The bitmap blitters can only draw lcd text to a N32 bitmap in srcOver.
  // Otherwise, convert the lcd text into A8 text. The props communicate this
  // to the scaler.
  const SurfaceProps& props = (ColorType::kN32 == color_type_ && paint.GetBlendMode() == BlendMode::kSrcOver)
                                  ? device_props_
                                  : bitmap_fallback_props_;

  ScalarPoint draw_origin = glyph_run_list.Origin();
  ScalarMatrix position_matrix{draw_matrix};
  position_matrix.PreTranslate(draw_origin.x, draw_origin.y);
  for (const GlyphRun& glyph_run : glyph_run_list) {
    const PlatformFont& run_font = glyph_run.Font();

    GlyphSource source{glyph_run.GlyphsIDs(), glyph_run.Positions()};

    if (StrikeSpec::ShouldDrawAsPath(paint, run_font, position_matrix)) {
      auto [strike_spec, strike_to_source_scale] =
          StrikeSpec::MakePath(run_font, paint, props, scaler_context_flags_);

      std::shared_ptr<Strike> strike = strike_spec.FindOrCreateStrike();

      {
        PreparedGlyphs prepared = PrepareForSourceSpaceDrawing(strike.get(), kPath, source, &buffers);

        source = prepared.rejected;
        // The paint we draw paths with must have the same anti-aliasing state
        // as the runFont allowing the paths to have the same edging as the
        // glyph masks.
        PlatformPaint path_paint = paint;
        path_paint.SetAntiAlias(run_font.HasSomeAntiAliasing());

        // The paint is always a fill without a path effect or a mask filter.
        const bool needs_exact_ctm = path_paint.GetShader() != nullptr;

        if (!needs_exact_ctm) {
          for (std::size_t i = 0; i < prepared.accepted_glyphs.size(); ++i) {
            const PlatformGlyph* glyph = prepared.accepted_glyphs[i];
            const ScalarPoint pos = prepared.accepted_positions[i];
            const ScalarPath* path = glyph->Path();
            ScalarPoint translate{draw_origin.x + pos.x, draw_origin.y + pos.y};
            ScalarMatrix m = MakeScaleTranslate(strike_to_source_scale, strike_to_source_scale,
                                                translate.x, translate.y);
            AutoCanvasRestore acr(canvas, true);
            canvas->Concat(m);
            canvas->DrawPath(*path, path_paint);
          }
        } else {
          for (std::size_t i = 0; i < prepared.accepted_glyphs.size(); ++i) {
            const PlatformGlyph* glyph = prepared.accepted_glyphs[i];
            const ScalarPoint pos = prepared.accepted_positions[i];
            const ScalarPath* path = glyph->Path();
            ScalarPoint translate{draw_origin.x + pos.x, draw_origin.y + pos.y};
            ScalarMatrix m = MakeScaleTranslate(strike_to_source_scale, strike_to_source_scale,
                                                translate.x, translate.y);

            ScalarPath builder = *path;
            builder.Transform(m);
            canvas->DrawPath(builder, path_paint);
          }
        }
      }

      if (!source.empty()) {
        PreparedGlyphs prepared = PrepareForSourceSpaceDrawing(strike.get(), kDrawable, source, &buffers);
        source = prepared.rejected;

        for (std::size_t i = 0; i < prepared.accepted_glyphs.size(); ++i) {
          const PlatformGlyph* glyph = prepared.accepted_glyphs[i];
          const ScalarPoint pos = prepared.accepted_positions[i];
          Drawable* drawable = glyph->GetDrawable();
          ScalarPoint translate{draw_origin.x + pos.x, draw_origin.y + pos.y};
          ScalarMatrix m = MakeScaleTranslate(strike_to_source_scale, strike_to_source_scale,
                                              translate.x, translate.y);
          AutoCanvasRestore acr(canvas, false);
          ScalarRect drawable_bounds = drawable->GetBounds();
          m.MapRect(&drawable_bounds);
          canvas->SaveLayer(&drawable_bounds, &paint);
          drawable->Draw(canvas, &m);
        }
      }
    }
    // The position matrix never has perspective.
    if (!source.empty()) {
      StrikeSpec strike_spec = StrikeSpec::MakeMask(run_font, paint, props, scaler_context_flags_, position_matrix);

      std::shared_ptr<Strike> strike = strike_spec.FindOrCreateStrike();

      PreparedGlyphs prepared = PrepareForDirectDrawing(strike.get(), position_matrix, source, &buffers,
                                                        /*map_accepted=*/true);
      source = prepared.rejected;
      bitmap_device->PaintMasks(prepared.accepted_glyphs, prepared.accepted_positions, paint);
    }
    if (!source.empty()) {
      // Create a strike is source space to calculate scale information.
      StrikeSpec scale_strike_spec = StrikeSpec::MakeMask(run_font, paint, props, scaler_context_flags_, ScalarMatrix());
      BulkGlyphMetrics metrics{scale_strike_spec};

      std::span<const std::uint16_t> glyph_ids = source.glyph_ids;
      std::span<const PlatformGlyph*> glyphs = metrics.Glyphs(glyph_ids);
      // SK_ScalarMin.
      float max_scale = -std::numeric_limits<float>::max();

      // Calculate the scale that makes the longest edge 1:1 with its side in
      // the cache.
      for (std::size_t i = 0; i < glyphs.size(); ++i) {
        const PlatformGlyph* glyph = glyphs[i];
        if (glyph->IsEmpty()) {
          continue;
        }
        ScalarRect rect = glyph->Rect();
        // Upstream calls rect.makeOffset(drawOrigin + pos) here and discards
        // the result, so the glyph rect is mapped without its position.
        // SkMatrix::mapRectToQuad.
        const std::array<ScalarPoint, 4> corners = {
            position_matrix.MapPoint({rect.left, rect.top}),
            position_matrix.MapPoint({rect.right, rect.top}),
            position_matrix.MapPoint({rect.right, rect.bottom}),
            position_matrix.MapPoint({rect.left, rect.bottom}),
        };
        // left top -> right top
        float scale = PointLength(corners[1].x - corners[0].x, corners[1].y - corners[0].y) / rect.Width();
        max_scale = std::max(max_scale, scale);
        // right top -> right bottom
        scale = PointLength(corners[2].x - corners[1].x, corners[2].y - corners[1].y) / rect.Height();
        max_scale = std::max(max_scale, scale);
        // right bottom -> left bottom
        scale = PointLength(corners[3].x - corners[2].x, corners[3].y - corners[2].y) / rect.Width();
        max_scale = std::max(max_scale, scale);
        // left bottom -> left top
        scale = PointLength(corners[0].x - corners[3].x, corners[0].y - corners[3].y) / rect.Height();
        max_scale = std::max(max_scale, scale);
      }

      if (max_scale <= 0) {
        continue; // to the next run.
      }

      if (max_scale * run_font.GetSize() > 256) {
        max_scale = 256.0f / run_font.GetSize();
      }

      ScalarMatrix cache_scale = ScalarMatrix::Scale(max_scale, max_scale);
      StrikeSpec strike_spec = StrikeSpec::MakeMask(run_font, paint, props, scaler_context_flags_, cache_scale);

      std::shared_ptr<Strike> strike = strike_spec.FindOrCreateStrike();

      PreparedGlyphs prepared = PrepareForDirectDrawing(strike.get(), position_matrix, source, &buffers,
                                                        /*map_accepted=*/false);
      const float inv_max_scale = 1.0f / max_scale;
      for (std::size_t i = 0; i < prepared.accepted_glyphs.size(); ++i) {
        const PlatformGlyph* glyph = prepared.accepted_glyphs[i];
        const ScalarPoint src_pos = prepared.accepted_positions[i];
        Mask mask = glyph->GetMask();
        // TODO: is this needed will A8 and BW just work?
        if (mask.format != MaskFormat::kARGB32) {
          continue;
        }
        std::shared_ptr<const Image> bm = MakeImageFromARGB32Mask(mask);
        if (!bm) {
          continue;
        }

        // Since the glyph in the cache is scaled by maxScale, its top left
        // vector is too long. Reduce it to find proper positions on the
        // device.
        ScalarPoint pos{draw_origin.x + src_pos.x + static_cast<float>(mask.bounds.left) * inv_max_scale,
                        draw_origin.y + src_pos.y + static_cast<float>(mask.bounds.top) * inv_max_scale};

        // Calculate the preConcat matrix for drawBitmap to get the rectangle
        // from the glyph cache (which is multiplied by maxScale) to land in
        // the right place.
        ScalarMatrix translate = ScalarMatrix::Translate(pos.x, pos.y);
        translate.PreScale(inv_max_scale, inv_max_scale);

        // Draw the bitmap using the rect from the scaled cache, and not the
        // source rectangle for the glyph.
        bitmap_device->DrawBitmap(std::move(bm), translate, nullptr, SamplingOptions{FilterMode::kLinear}, paint);
      }
    }

    // TODO: have the mask stage above reject the glyphs that are too big, and
    // handle the rejects in a more sophisticated stage.
  }
}

} // namespace bkfont
