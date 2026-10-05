// Local implementation: the CPU canvas that draws glyph images.
// Ported from: skia/src/core/SkCanvas.cpp
// Ported from: skia/src/core/SkDevice.cpp
// Ported from: skia/src/core/SkDraw.cpp
// Ported from: skia/src/core/SkDraw_text.cpp
// Ported from: skia/src/core/SkBlitter.cpp
// Ported from: skia/src/core/SkBlitter_ARGB32.cpp
// Ported from: skia/src/core/SkRasterPipelineBlitter.cpp

#include "raster_canvas.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>

#include "glyph_run.h"
#include "platform_glyph.h"
#include "text_blob.h"

namespace bkfont {

namespace {

IntRect Intersect(const IntRect& a, const IntRect& b) {
  IntRect r = IntRect::MakeLTRB(std::max(a.left, b.left), std::max(a.top, b.top),
                                std::min(a.right, b.right), std::min(a.bottom, b.bottom));
  if (r.IsEmpty()) {
    return IntRect();
  }
  return r;
}

IntRect RoundOut(const ScalarRect& r) {
  const auto to_int = [](float v) {
    return static_cast<std::int32_t>(std::clamp<double>(v, -(1 << 29), 1 << 29));
  };
  return IntRect::MakeLTRB(to_int(std::floor(r.left)), to_int(std::floor(r.top)),
                           to_int(std::ceil(r.right)), to_int(std::ceil(r.bottom)));
}

float Clamp01(float v) {
  return std::clamp(v, 0.0f, 1.0f);
}

PMColor4f Clamp01(const PMColor4f& c) {
  return {Clamp01(c.r), Clamp01(c.g), Clamp01(c.b), Clamp01(c.a)};
}

// SkBlendMode_ShouldPreScaleCoverage: these modes scale the source by the
// coverage instead of lerping the result.
bool ShouldPreScaleCoverage(BlendMode mode, bool rgb_coverage = false) {
  switch (mode) {
  case BlendMode::kDst:     // d              --> no sa term, ok!
  case BlendMode::kDstOver: // d + s*inv(da)  --> no sa term, ok!
  case BlendMode::kPlus:    // clamp(s+d)     --> no sa term, ok!
    return true;

  case BlendMode::kDstOut:  // d * inv(sa)
  case BlendMode::kSrcATop: // s*da + d*inv(sa)
  case BlendMode::kSrcOver: // s + d*inv(sa)
  case BlendMode::kXor:     // s*inv(da) + d*inv(sa)
    return !rgb_coverage;

  default:
    break;
  }
  return false;
}

PMColor4f BlendWithCoverage(BlendMode mode, const PMColor4f& src, const PMColor4f& dst, float coverage) {
  if (coverage >= 1) {
    return Clamp01(BlendPixel(mode, src, dst));
  }
  if (ShouldPreScaleCoverage(mode)) {
    return Clamp01(BlendPixel(mode, src * coverage, dst));
  }
  const PMColor4f blended = BlendPixel(mode, src, dst);
  return Clamp01(PMColor4f{dst.r + (blended.r - dst.r) * coverage, dst.g + (blended.g - dst.g) * coverage,
                           dst.b + (blended.b - dst.b) * coverage, dst.a + (blended.a - dst.a) * coverage});
}

// alpha_coverage_from_rgb_coverage of the raster pipeline.
float AlphaCoverageFromRGBCoverage(float a, float da, float cr, float cg, float cb) {
  return a < da ? std::min(cr, std::min(cg, cb)) : std::max(cr, std::max(cg, cb));
}

float Lerp(float from, float to, float t) {
  return (to - from) * t + from;
}

// The kLCD16 pipelines of SkRasterPipelineBlitter::blitMask: scale_565 or
// lerp_565 with per-channel coverage.
PMColor4f BlendLCDWithCoverage(BlendMode mode, const PMColor4f& src, const PMColor4f& dst,
                               float cr, float cg, float cb) {
  if (ShouldPreScaleCoverage(mode, /*rgb_coverage=*/true)) {
    // Somewhat unusually, scale_565 needs dst loaded first.
    const float ca = AlphaCoverageFromRGBCoverage(src.a, dst.a, cr, cg, cb);
    return Clamp01(BlendPixel(mode, PMColor4f{src.r * cr, src.g * cg, src.b * cb, src.a * ca}, dst));
  }
  const PMColor4f blended = BlendPixel(mode, src, dst);
  const float ca = AlphaCoverageFromRGBCoverage(blended.a, dst.a, cr, cg, cb);
  return Clamp01(PMColor4f{Lerp(dst.r, blended.r, cr), Lerp(dst.g, blended.g, cg),
                           Lerp(dst.b, blended.b, cb), Lerp(dst.a, blended.a, ca)});
}

// blend_lcd16 and blend_lcd16_opaque of the legacy SkARGB32 blitters for a
// solid color. The coverages are scaled by the source alpha; the colors move
// from dst towards the unpremultiplied source by them, and the alpha uses
// either the min or the max of them. See https:/skbug.com/40037823
PMColor4f BlendLCD16Color(const PMColor4f& src, const PMColor4f& dst, float cr, float cg, float cb) {
  const float sa = src.a;
  const float mr = cr * sa;
  const float mg = cg * sa;
  const float mb = cb * sa;
  const float ma = sa < dst.a ? std::min(mr, std::min(mg, mb)) : std::max(mr, std::max(mg, mb));
  // (unpremul(src) - dst) * c * sa, with src premultiplied.
  return Clamp01(PMColor4f{dst.r + (src.r - sa * dst.r) * cr,
                           dst.g + (src.g - sa * dst.g) * cg,
                           dst.b + (src.b - sa * dst.b) * cb,
                           dst.a + (1 - dst.a) * ma});
}

// blend_row_lcd16 and blend_row_LCD16_opaque of SkARGB32_Shader_Blitter.
// This LCD blit routine only works if the destination is opaque, so the
// alpha is always written opaque.
PMColor4f BlendLCD16Shader(const PMColor4f& src, const PMColor4f& dst, float cr, float cg, float cb) {
  const float sa = src.a;
  return Clamp01(PMColor4f{dst.r + (src.r - sa * dst.r) * cr,
                           dst.g + (src.g - sa * dst.g) * cg,
                           dst.b + (src.b - sa * dst.b) * cb,
                           1});
}

// The coverage of one pixel of a kBW, kA8 or kLCD16 mask. The rgb coverages
// are equal unless the mask is kLCD16.
struct MaskCoverage {
  float r = 0;
  float g = 0;
  float b = 0;
};

MaskCoverage MaskCoverageAt(const Mask& mask, int x, int y, bool legacy_lcd) {
  const std::uint8_t* row = mask.image + static_cast<std::size_t>(y - mask.bounds.top) * mask.row_bytes;
  const std::size_t dx = static_cast<std::size_t>(x - mask.bounds.left);
  switch (mask.format) {
  case MaskFormat::kBW: {
    // SkMask::getAddr1, most significant bit first.
    const float c = ((row[dx >> 3] >> (7 - (dx & 7))) & 1) ? 1.0f : 0.0f;
    return {c, c, c};
  }
  case MaskFormat::kA8: {
    // from_byte.
    const float c = row[dx] * (1 / 255.0f);
    return {c, c, c};
  }
  case MaskFormat::kLCD16: {
    std::uint16_t m;
    std::memcpy(&m, row + dx * sizeof(std::uint16_t), sizeof(m));
    if (legacy_lcd) {
      // We're ignoring the least significant bit of the green coverage
      // channel here.
      return {static_cast<float>((m >> 11) & 31) * (1 / 31.0f),
              static_cast<float>((m >> 6) & 31) * (1 / 31.0f),
              static_cast<float>(m & 31) * (1 / 31.0f)};
    }
    // from_565.
    return {static_cast<float>(m & (31 << 11)) * (1.0f / (31 << 11)),
            static_cast<float>(m & (63 << 5)) * (1.0f / (63 << 5)),
            static_cast<float>(m & (31 << 0)) * (1.0f / (31 << 0))};
  }
  default:
    break;
  }
  return {};
}

// check_glyph_position.
bool CheckGlyphPosition(ScalarPoint position) {
  // Prevent glyphs from being drawn outside of or straddling the edge of
  // device space. Comparisons written a little weirdly so that NaN
  // coordinates are treated safely.
  auto gt = [](float a, int b) {
    return !(a <= static_cast<float>(b));
  };
  auto lt = [](float a, int b) {
    return !(a >= static_cast<float>(b));
  };
  return !(gt(position.x, INT_MAX - (INT16_MAX + static_cast<int>(UINT16_MAX))) ||
           lt(position.x, INT_MIN - (INT16_MIN + 0 /*UINT16_MIN*/)) ||
           gt(position.y, INT_MAX - (INT16_MAX + static_cast<int>(UINT16_MAX))) ||
           lt(position.y, INT_MIN - (INT16_MIN + 0 /*UINT16_MIN*/)));
}

// SkIRect::containsNoEmptyCheck.
bool ContainsNoEmptyCheck(const IntRect& outer, const IntRect& inner) {
  return outer.left <= inner.left && outer.top <= inner.top && outer.right >= inner.right && outer.bottom >= inner.bottom;
}

// SkRect::isFinite.
bool IsFinite(const ScalarRect& r) {
  float accum = 0;
  accum *= r.left;
  accum *= r.top;
  accum *= r.right;
  accum *= r.bottom;
  // accum is either NaN or it is finite (zero).
  return !std::isnan(accum);
}

// make_post_inverse_lm.
std::shared_ptr<const Shader> MakePostInverseLocalMatrix(const std::shared_ptr<const Shader>& shader,
                                                         const ScalarMatrix& lm) {
  ScalarMatrix inverse_lm;
  if (!shader || !lm.Invert(&inverse_lm)) {
    return nullptr;
  }
  return MakeWithLocalMatrix(shader, inverse_lm);
}

// SkPaint::nothingToDraw.
bool NothingToDraw(const PlatformPaint& paint) {
  switch (paint.GetBlendMode()) {
  case BlendMode::kSrcOver:
  case BlendMode::kSrcATop:
  case BlendMode::kDstOut:
  case BlendMode::kDstOver:
  case BlendMode::kPlus:
    // getAlpha() is the 8-bit alpha.
    return ((paint.GetColor() >> 24) & 0xFF) == 0;
  case BlendMode::kDst:
    return true;
  default:
    break;
  }
  return false;
}

std::uint8_t ToByte(float v) {
  return static_cast<std::uint8_t>(Clamp01(v) * 255 + 0.5f);
}

} // namespace

RasterCanvas::RasterCanvas(const Pixmap& pixmap)
    : RasterCanvas(pixmap, SurfaceProps()) {
}

RasterCanvas::RasterCanvas(const Pixmap& pixmap, const SurfaceProps& props)
    : pixmap_(pixmap), device_bounds_(IntRect::MakeWH(pixmap.Width(), pixmap.Height())) {
  base_.bounds = device_bounds_;
  base_.color_type = pixmap.GetColorType();
  base_.props = props;
  if (!device_bounds_.IsEmpty()) {
    base_.pixels.resize(static_cast<std::size_t>(device_bounds_.Width()) * static_cast<std::size_t>(device_bounds_.Height()));
    for (int y = 0; y < pixmap_.Height(); ++y) {
      for (int x = 0; x < pixmap_.Width(); ++x) {
        base_.At(x, y) = pixmap_.GetPMColor4f(x, y);
      }
    }
  } else {
    base_.bounds = IntRect();
  }
  State state;
  state.clip.bounds = base_.bounds;
  states_.push_back(std::move(state));
}

RasterCanvas::~RasterCanvas() {
  // SkCanvas restores every outstanding layer when it is destroyed.
  RestoreToCount(1);
  Flush();
}

void RasterCanvas::Flush() {
  for (int y = 0; y < pixmap_.Height(); ++y) {
    for (int x = 0; x < pixmap_.Width(); ++x) {
      const PMColor4f& c = base_.At(x, y);
      std::uint8_t* p = pixmap_.WritableAddr8(x, y);
      if (pixmap_.GetColorType() == ColorType::kAlpha8) {
        p[0] = ToByte(c.a);
      } else {
        const PMColor packed = PackARGB32(ToByte(c.a), ToByte(c.r), ToByte(c.g), ToByte(c.b));
        std::memcpy(p, &packed, sizeof(packed));
      }
    }
  }
}

int RasterCanvas::Save() {
  const int previous = GetSaveCount();
  State state;
  state.matrix = states_.back().matrix;
  state.clip = states_.back().clip;
  states_.push_back(std::move(state));
  return previous;
}

int RasterCanvas::SaveLayer(const ScalarRect* bounds, const PlatformPaint* paint) {
  const int previous = GetSaveCount();
  const State& top = states_.back();

  IntRect layer_bounds = top.clip.bounds;
  // SkCanvas ignores the content bounds hint when restoring transparent
  // pixels can also change the destination outside those bounds.
  if (bounds && (!paint || !BlendModeAffectsTransparentBlack(paint->GetBlendMode()))) {
    ScalarRect mapped = *bounds;
    top.matrix.MapRect(&mapped);
    layer_bounds = Intersect(layer_bounds, RoundOut(mapped));
  }

  auto layer = std::make_unique<Layer>();
  layer->bounds = layer_bounds;
  if (!layer_bounds.IsEmpty()) {
    layer->pixels.assign(static_cast<std::size_t>(layer_bounds.Width()) * static_cast<std::size_t>(layer_bounds.Height()), PMColor4f{});
  }
  if (paint) {
    layer->blend_mode = paint->GetBlendMode();
    layer->alpha = paint->GetAlphaf();
  }
  // Without kPreserveLCDText_SaveLayerFlag the layer device has an unknown
  // pixel geometry. A8 and other narrow devices are upgraded to N32.
  layer->color_type = ColorType::kN32;
  layer->props = TopLayer().props.CloneWithPixelGeometry(PixelGeometry::kUnknown);

  State state;
  state.matrix = top.matrix;
  // A new SkBitmapDevice starts with a rectangular device clip. The saved
  // clip, including AA coverage, is applied once when restoring this layer.
  state.clip.bounds = layer_bounds;
  state.layer = std::move(layer);
  states_.push_back(std::move(state));
  return previous;
}

void RasterCanvas::Restore() {
  if (states_.size() <= 1) {
    return;
  }
  std::unique_ptr<Layer> layer = std::move(states_.back().layer);
  states_.pop_back();
  if (layer) {
    // The restored clip is the clip in effect when the layer was saved.
    CompositeLayer(*layer, states_.back().clip, &TopLayer());
  }
}

int RasterCanvas::GetSaveCount() const {
  return static_cast<int>(states_.size());
}

void RasterCanvas::Concat(const ScalarMatrix& matrix) {
  states_.back().matrix.PreConcat(matrix);
}

const ScalarMatrix& RasterCanvas::GetTotalMatrix() const {
  return states_.back().matrix;
}

void RasterCanvas::ClipRect(const ScalarRect& rect, bool do_anti_alias) {
  ClipPath(ScalarPath::Rect(rect), do_anti_alias);
}

void RasterCanvas::ClipPath(const ScalarPath& path, bool do_anti_alias) {
  CoverageMask mask;
  RasterizePath(path, states_.back().matrix, states_.back().clip.bounds, do_anti_alias, &mask);
  IntersectClip(mask);
}

void RasterCanvas::IntersectClip(const CoverageMask& mask) {
  Clip& clip = states_.back().clip;
  Clip result;
  result.bounds = mask.bounds;
  if (!result.bounds.IsEmpty()) {
    auto coverage = std::make_shared<std::vector<float>>(
        static_cast<std::size_t>(device_bounds_.Width()) * static_cast<std::size_t>(device_bounds_.Height()), 0.0f);
    for (int y = result.bounds.top; y < result.bounds.bottom; ++y) {
      for (int x = result.bounds.left; x < result.bounds.right; ++x) {
        (*coverage)[static_cast<std::size_t>(y) * static_cast<std::size_t>(device_bounds_.Width()) + static_cast<std::size_t>(x)] =
            mask.At(x, y) * ClipCoverage(clip, x, y);
      }
    }
    result.coverage = std::move(coverage);
  }
  clip = std::move(result);
}

float RasterCanvas::ClipCoverage(const Clip& clip, int x, int y) const {
  if (x < clip.bounds.left || x >= clip.bounds.right || y < clip.bounds.top || y >= clip.bounds.bottom) {
    return 0;
  }
  if (!clip.coverage) {
    return 1;
  }
  return (*clip.coverage)[static_cast<std::size_t>(y) * static_cast<std::size_t>(device_bounds_.Width()) + static_cast<std::size_t>(x)];
}

RasterCanvas::Layer& RasterCanvas::TopLayer() {
  for (auto it = states_.rbegin(); it != states_.rend(); ++it) {
    if (it->layer) {
      return *it->layer;
    }
  }
  return base_;
}

void RasterCanvas::DrawPaint(const PlatformPaint& paint) {
  if (NothingToDraw(paint)) {
    return;
  }
  Fill(nullptr, paint, states_.back().matrix);
}

void RasterCanvas::DrawPath(const ScalarPath& path, const PlatformPaint& paint) {
  if (NothingToDraw(paint)) {
    return;
  }
  CoverageMask mask;
  RasterizePath(path, states_.back().matrix, states_.back().clip.bounds, paint.IsAntiAlias(), &mask);
  if (mask.IsEmpty()) {
    return;
  }
  Fill(&mask, paint, states_.back().matrix);
}

void RasterCanvas::DrawImage(std::shared_ptr<const Image> image, float x, float y,
                             const SamplingOptions& sampling, const PlatformPaint* paint) {
  if (!image) {
    return;
  }
  // drawImageRect with the image bounds as the destination, drawn as a rect
  // filled by an image shader.
  const ScalarRect dst = ScalarRect::MakeXYWH(x, y, static_cast<float>(image->Width()), static_cast<float>(image->Height()));
  PlatformPaint real_paint = paint ? *paint : PlatformPaint();
  real_paint.SetShader(MakeImageShader(std::move(image), sampling, ScalarMatrix::Translate(x, y)));
  DrawPath(ScalarPath::Rect(dst), real_paint);
}

void RasterCanvas::Fill(const CoverageMask* geometry, const PlatformPaint& paint, const ScalarMatrix& ctm) {
  const State& state = states_.back();
  Layer& layer = TopLayer();

  IntRect area = Intersect(layer.bounds, state.clip.bounds);
  if (geometry) {
    area = Intersect(area, geometry->bounds);
  }
  if (area.IsEmpty()) {
    return;
  }

  std::unique_ptr<Shader::Context> context;
  PMColor4f solid;
  // SkBlitter::Choose discards the shader for Clear.
  if (paint.GetShader() && paint.GetBlendMode() != BlendMode::kClear) {
    context = paint.GetShader()->MakeContext(ctm, paint.GetColor4f());
    if (!context) {
      return;
    }
  } else {
    solid = Clamp01(paint.GetColor4f().Premul());
  }

  const BlendMode mode = paint.GetBlendMode();
  std::vector<PMColor4f> span(static_cast<std::size_t>(area.Width()));
  for (int y = area.top; y < area.bottom; ++y) {
    if (context) {
      context->ShadeSpan(area.left, y, area.Width(), span.data());
    }
    for (int x = area.left; x < area.right; ++x) {
      float coverage = ClipCoverage(state.clip, x, y);
      if (geometry) {
        coverage *= geometry->At(x, y);
      }
      if (coverage <= 0) {
        continue;
      }
      const PMColor4f& src = context ? span[static_cast<std::size_t>(x - area.left)] : solid;
      PMColor4f& dst = layer.At(x, y);
      dst = BlendWithCoverage(mode, src, dst, coverage);
    }
  }
}

void RasterCanvas::OnDrawGlyphRunList(const GlyphRunList& glyph_run_list, const PlatformPaint& paint) {
  ScalarRect bounds = glyph_run_list.SourceBoundsWithOrigin();
  if (InternalQuickReject(bounds, paint)) {
    return;
  }

  // aboutToDraw starts no auto layer: paints have no image filter or mask
  // filter.
  DrawGlyphRunList(glyph_run_list, paint);
}

bool RasterCanvas::InternalQuickReject(const ScalarRect& bounds, const PlatformPaint& paint) const {
  if (!IsFinite(bounds) || NothingToDraw(paint)) {
    return true;
  }

  // The paint is a fill without effects, so its fast bounds are the bounds.
  return QuickReject(bounds);
}

bool RasterCanvas::QuickReject(const ScalarRect& src) const {
  // fQuickRejectBounds: computeDeviceClipBounds expanded by 1 in case we are
  // anti-aliasing. An empty clip has empty bounds, which nothing intersects.
  const IntRect& clip_bounds = states_.back().clip.bounds;
  if (clip_bounds.IsEmpty()) {
    return true;
  }
  ScalarRect quick_reject_bounds = ScalarRect::MakeLTRB(static_cast<float>(clip_bounds.left),
                                                        static_cast<float>(clip_bounds.top),
                                                        static_cast<float>(clip_bounds.right),
                                                        static_cast<float>(clip_bounds.bottom));
  quick_reject_bounds.Outset(1.f, 1.f);

  ScalarRect dev_rect = src;
  states_.back().matrix.MapRect(&dev_rect);
  if (!IsFinite(dev_rect)) {
    return true;
  }
  // SkRect::intersects.
  const float l = std::max(dev_rect.left, quick_reject_bounds.left);
  const float r = std::min(dev_rect.right, quick_reject_bounds.right);
  const float t = std::max(dev_rect.top, quick_reject_bounds.top);
  const float b = std::min(dev_rect.bottom, quick_reject_bounds.bottom);
  return !(l < r && t < b);
}

void RasterCanvas::DrawGlyphRunList(const GlyphRunList& glyph_run_list, const PlatformPaint& paint) {
  if (!states_.back().matrix.IsFinite()) {
    return;
  }

  if (!glyph_run_list.HasRSXForm()) {
    // SkBitmapDevice::onDrawGlyphRunList and SkDraw::drawGlyphRunList.
    if (states_.back().clip.bounds.IsEmpty()) {
      return;
    }
    // Copies, since drawing paths and drawables saves and restores.
    const ScalarMatrix draw_matrix = states_.back().matrix;
    const Layer& layer = TopLayer();
    GlyphRunListPainterCPU glyph_painter(layer.props, layer.color_type);
    glyph_painter.DrawForBitmapDevice(this, this, glyph_run_list, paint, draw_matrix);
  } else {
    SimplifyGlyphRunRSXFormAndRedraw(glyph_run_list, paint);
  }
}

void RasterCanvas::SimplifyGlyphRunRSXFormAndRedraw(const GlyphRunList& glyph_run_list, const PlatformPaint& paint) {
  for (const GlyphRun& run : glyph_run_list) {
    if (run.ScaledRotations().empty()) {
      GlyphRunList sub_list = glyph_run_list.Builder()->MakeGlyphRunList(run, paint, {0, 0});
      DrawGlyphRunList(sub_list, paint);
    } else {
      ScalarPoint origin = glyph_run_list.Origin();
      ScalarPoint shared_pos{0, 0}; // we're at the origin
      std::uint16_t shared_glyph_id = 0;
      GlyphRun glyph_run{run.Font(),
                         std::span<const ScalarPoint>{&shared_pos, 1},
                         std::span<const std::uint16_t>{&shared_glyph_id, 1},
                         std::span<const char>{},
                         std::span<const std::uint32_t>{},
                         std::span<const ScalarPoint>{}};

      for (std::size_t i = 0; i < run.RunSize(); ++i) {
        shared_glyph_id = run.GlyphsIDs()[i];
        const ScalarPoint pos = run.Positions()[i];
        const ScalarPoint scaled_rotation = run.ScaledRotations()[i];
        RSXform rsx_form = RSXform::Make(scaled_rotation.x, scaled_rotation.y, pos.x, pos.y);
        // SkMatrix::setRSXform.
        ScalarMatrix glyph_to_local = ScalarMatrix::MakeAll(rsx_form.scos, -rsx_form.ssin, rsx_form.tx,
                                                            rsx_form.ssin, rsx_form.scos, rsx_form.ty);
        glyph_to_local.PostTranslate(origin.x, origin.y);

        // We want to rotate each glyph by the rsxform, but we don't want to
        // rotate "space" (i.e. the shader that cares about the ctm) so we
        // have to undo our little ctm trick with a localmatrixshader so that
        // the shader draws as if there was no change to the ctm.
        PlatformPaint inverting_paint{paint};
        inverting_paint.SetShader(MakePostInverseLocalMatrix(paint.GetShader(), glyph_to_local));
        AutoCanvasRestore acr(this, true);
        Concat(glyph_to_local);
        GlyphRunList sub_list = glyph_run_list.Builder()->MakeGlyphRunList(glyph_run, paint, {0, 0});
        DrawGlyphRunList(sub_list, inverting_paint);
      }
    }
  }
}

void RasterCanvas::PaintMasks(std::span<const PlatformGlyph* const> glyphs,
                              std::span<const ScalarPoint> positions,
                              const PlatformPaint& paint) {
  // SkBlitter::Choose. The clip is applied per pixel, as the AA clip blitter
  // wrapper does; a BW clip region has coverage 0 or 1.
  std::unique_ptr<Shader::Context> context;
  PMColor4f solid;
  bool can_blit = true;
  // Clear ignores the color pipeline, including an empty shader.
  if (paint.GetShader() && paint.GetBlendMode() != BlendMode::kClear) {
    context = paint.GetShader()->MakeContext(states_.back().matrix, paint.GetColor4f());
    // A shader that cannot draw makes a null blitter.
    can_blit = context != nullptr;
  } else {
    solid = Clamp01(paint.GetColor4f().Premul());
  }

  const IntRect clip_bounds = states_.back().clip.bounds;
  for (std::size_t i = 0; i < glyphs.size(); ++i) {
    const PlatformGlyph* glyph = glyphs[i];
    const ScalarPoint pos = positions[i];
    if (CheckGlyphPosition(pos)) {
      Mask mask = glyph->GetMask(pos);
      IntRect bounds = mask.bounds;

      // this extra test is worth it, assuming that most of the time it
      // succeeds since we can avoid writing to storage
      if (!ContainsNoEmptyCheck(clip_bounds, mask.bounds)) {
        bounds = Intersect(mask.bounds, clip_bounds);
        if (bounds.IsEmpty()) {
          continue;
        }
      }

      if (MaskFormat::kARGB32 == mask.format) {
        DrawSprite(MakeImageFromARGB32Mask(mask), mask.bounds.left, mask.bounds.top, paint);
      } else if (can_blit) {
        BlitMask(mask, bounds, paint, context.get(), solid);
      }
    }
  }
}

void RasterCanvas::DrawBitmap(std::shared_ptr<const Image> image, const ScalarMatrix& prematrix,
                              const ScalarRect* dst_or_null, const SamplingOptions& sampling,
                              const PlatformPaint& paint) {
  // The destination bounds only save the bounds computation upstream.
  (void)dst_or_null;
  // nothing to draw
  if (states_.back().clip.bounds.IsEmpty() || !image || image->Width() == 0 || image->Height() == 0) {
    return;
  }

  ScalarMatrix matrix = states_.back().matrix;
  matrix.PreConcat(prematrix);

  // The image is never alpha only. SkTreatAsSprite only accepts integral
  // translations with linear sampling, which the image shader draws with the
  // same pixels as a sprite.
  PlatformPaint paint_with_shader = paint;
  const ScalarRect src_bounds = ScalarRect::MakeXYWH(0, 0, static_cast<float>(image->Width()), static_cast<float>(image->Height()));
  paint_with_shader.SetShader(MakeImageShader(std::move(image), sampling, ScalarMatrix()));

  CoverageMask mask;
  RasterizePath(ScalarPath::Rect(src_bounds), matrix, states_.back().clip.bounds, paint_with_shader.IsAntiAlias(), &mask);
  if (mask.IsEmpty()) {
    return;
  }
  Fill(&mask, paint_with_shader, matrix);
}

void RasterCanvas::DrawSprite(std::shared_ptr<const Image> image, int x, int y, const PlatformPaint& paint) {
  // nothing to draw
  if (states_.back().clip.bounds.IsEmpty() || !image || image->Width() == 0 || image->Height() == 0) {
    return;
  }

  const IntRect bounds = IntRect::MakeXYWH(x, y, image->Width(), image->Height());
  if (!IntRect::Intersects(bounds, states_.back().clip.bounds)) {
    return; // nothing to draw
  }

  // The sprite blitters draw the same pixels as the image shader offset to
  // the integral device position.
  ScalarRect r = ScalarRect::MakeLTRB(static_cast<float>(bounds.left), static_cast<float>(bounds.top),
                                      static_cast<float>(bounds.right), static_cast<float>(bounds.bottom));

  // create shader with offset
  ScalarMatrix matrix = ScalarMatrix::Translate(r.left, r.top);
  PlatformPaint paint_with_shader = paint;
  paint_with_shader.SetShader(MakeImageShader(std::move(image), SamplingOptions(), matrix));

  // call ourself with a rect, in device space
  CoverageMask mask;
  RasterizePath(ScalarPath::Rect(r), ScalarMatrix(), states_.back().clip.bounds, paint_with_shader.IsAntiAlias(), &mask);
  if (mask.IsEmpty()) {
    return;
  }
  Fill(&mask, paint_with_shader, ScalarMatrix());
}

void RasterCanvas::BlitMask(const Mask& mask, const IntRect& clip, const PlatformPaint& paint,
                            Shader::Context* context, const PMColor4f& solid) {
  if (mask.format != MaskFormat::kBW && mask.format != MaskFormat::kA8 && mask.format != MaskFormat::kLCD16) {
    // ARGB and SDF masks shouldn't make it here; glyphs never have k3D
    // masks without a mask filter.
    return;
  }

  const State& state = states_.back();
  Layer& layer = TopLayer();
  const IntRect area = Intersect(Intersect(clip, layer.bounds), state.clip.bounds);
  if (area.IsEmpty() || mask.image == nullptr) {
    return;
  }

  const BlendMode mode = paint.GetBlendMode();
  // SkBlitter::UseLegacyBlitter: N32 devices drawn with kSrcOver use the
  // legacy SkARGB32 blitters, which blend LCD masks their own way. Shaders
  // without a legacy context (including gradients) use the raster pipeline.
  const bool legacy = layer.color_type == ColorType::kN32 && mode == BlendMode::kSrcOver &&
                      (!paint.GetShader() || paint.GetShader()->CanMakeLegacyContext(state.matrix));

  std::vector<PMColor4f> span(static_cast<std::size_t>(area.Width()));
  for (int y = area.top; y < area.bottom; ++y) {
    if (context) {
      context->ShadeSpan(area.left, y, area.Width(), span.data());
    }
    for (int x = area.left; x < area.right; ++x) {
      const float clip_coverage = ClipCoverage(state.clip, x, y);
      if (clip_coverage <= 0) {
        continue;
      }
      MaskCoverage coverage = MaskCoverageAt(mask, x, y, legacy);
      // The AA clip is merged into the mask.
      coverage.r *= clip_coverage;
      coverage.g *= clip_coverage;
      coverage.b *= clip_coverage;
      if (coverage.r <= 0 && coverage.g <= 0 && coverage.b <= 0) {
        continue;
      }

      const PMColor4f& src = context ? span[static_cast<std::size_t>(x - area.left)] : solid;
      PMColor4f& dst = layer.At(x, y);
      if (mask.format != MaskFormat::kLCD16) {
        dst = BlendWithCoverage(mode, src, dst, coverage.r);
      } else if (!legacy) {
        dst = BlendLCDWithCoverage(mode, src, dst, coverage.r, coverage.g, coverage.b);
      } else if (context) {
        dst = BlendLCD16Shader(src, dst, coverage.r, coverage.g, coverage.b);
      } else {
        dst = BlendLCD16Color(src, dst, coverage.r, coverage.g, coverage.b);
      }
    }
  }
}

void RasterCanvas::CompositeLayer(const Layer& layer, const Clip& clip, Layer* target) {
  const IntRect area = Intersect(Intersect(layer.bounds, target->bounds), clip.bounds);
  if (area.IsEmpty()) {
    return;
  }
  for (int y = area.top; y < area.bottom; ++y) {
    for (int x = area.left; x < area.right; ++x) {
      const float coverage = ClipCoverage(clip, x, y);
      if (coverage <= 0) {
        continue;
      }
      const PMColor4f src = layer.At(x, y) * layer.alpha;
      PMColor4f& dst = target->At(x, y);
      dst = BlendWithCoverage(layer.blend_mode, src, dst, coverage);
    }
  }
}

} // namespace bkfont
