// Ported from: skia/src/core/SkScalerContext.cpp

#include "scaler_context.h"

#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

#include "base/hash/hash.h"
#include "font_host_freetype.h"
#include "scalar.h"

namespace bkfont {

namespace {

template <typename D, typename S>
constexpr D SaturateCast(S s) {
  static_assert(std::is_integral_v<D>);
  s = s < std::numeric_limits<D>::max() ? s : std::numeric_limits<D>::max();
  s = s > std::numeric_limits<D>::min() ? s : std::numeric_limits<D>::min();
  return static_cast<D>(s);
}

MaskFormat ComputeMaskFormat(const PlatformFont& font) {
  switch (font.GetEdging()) {
    case PlatformFont::Edging::kAlias:
      return MaskFormat::kBW;
    case PlatformFont::Edging::kAntiAlias:
      return MaskFormat::kA8;
    case PlatformFont::Edging::kSubpixelAntiAlias:
      return MaskFormat::kLCD16;
  }
  return MaskFormat::kA8;
}

// SkFontPriv::MakeTextMatrix.
ScalarMatrix MakeTextMatrix(float size, float scale_x, float skew_x) {
  ScalarMatrix m = ScalarMatrix::Scale(size * scale_x, size);
  if (skew_x) {
    m.PostSkew(skew_x, 0);
  }
  return m;
}

class EmptyScalerContext final : public ScalerContext {
public:
  EmptyScalerContext(std::shared_ptr<FontFace> typeface, const ScalerContextRec& rec)
      : ScalerContext(std::move(typeface), rec) {
  }

protected:
  GlyphMetrics GenerateMetrics(const PlatformGlyph& glyph) override {
    return GlyphMetrics(glyph.GetMaskFormat());
  }
};

} // namespace

FontHinting ScalerContextRec::GetHinting() const {
  return static_cast<FontHinting>((flags & ScalerContext::kHinting_Mask) >> ScalerContext::kHinting_Shift);
}

void ScalerContextRec::SetHinting(FontHinting hinting) {
  flags = static_cast<std::uint16_t>((flags & ~ScalerContext::kHinting_Mask) | (static_cast<unsigned>(hinting) << ScalerContext::kHinting_Shift));
}

ScalarMatrix ScalerContextRec::GetMatrixFrom2x2() const {
  return ScalarMatrix::MakeAll(post2x2[0][0], post2x2[0][1], post2x2[1][0], post2x2[1][1]);
}

ScalarMatrix ScalerContextRec::GetLocalMatrix() const {
  return MakeTextMatrix(text_size, pre_scale_x, pre_skew_x);
}

ScalarMatrix ScalerContextRec::GetSingleMatrix() const {
  return GetLocalMatrix().PostConcat(GetMatrixFrom2x2());
}

bool ScalerContextRec::ComputeMatrices(ScalarPoint* s, ScalarMatrix* s_a) const {
  // A is the 'total' matrix.
  const ScalarMatrix a = GetSingleMatrix();

  // GA is the matrix A with rotation removed.
  ScalarMatrix ga;
  const bool skewed_or_flipped = a.GetSkewX() || a.GetSkewY() || a.GetScaleX() < 0 || a.GetScaleY() < 0;
  if (skewed_or_flipped) {
    // QR by Givens rotations. G is Q^T and GA is R. G is rotational (no
    // reflections). h is where A maps the horizontal baseline.
    const ScalarPoint h = a.MapPoint({1, 0});

    // G is the Givens Matrix for A (rotational matrix where GA[0][1] == 0).
    ScalarMatrix g;
    ComputeGivensRotation(h, &g);

    ga = g;
    ga.PreConcat(a);
  } else {
    ga = a;
  }

  // If the 'total' matrix is singular, set the 'scale' to something finite
  // and zero the matrices. All underlying ports have issues with zero text
  // size, so use the matricies to zero. If there are any nonfinite numbers in
  // the matrix, bail out and set the matrices to zero.
  if (std::abs(ga.GetScaleX()) <= kScalarNearlyZero || std::abs(ga.GetScaleY()) <= kScalarNearlyZero || !ga.IsFinite()) {
    s->x = 1;
    s->y = 1;
    s_a->SetScale(0, 0);
    return false;
  }

  // At this point, given GA, create s.
  s->x = std::abs(ga.GetScaleX());
  s->y = std::abs(ga.GetScaleY());

  // The 'remaining' matrix sA is the total matrix A without the scale.
  if (!skewed_or_flipped) {
    // If GA == A and kFull, sA is identity.
    s_a->Reset();
  } else {
    *s_a = a;
    s_a->PreScale(1 / s->x, 1 / s->y);
  }
  return true;
}

bool ScalerContextRec::operator==(const ScalerContextRec& other) const {
  const auto bits = [](float value) {
    return std::bit_cast<std::uint32_t>(value);
  };
  return typeface_id == other.typeface_id && bits(text_size) == bits(other.text_size) && bits(pre_scale_x) == bits(other.pre_scale_x) && bits(pre_skew_x) == bits(other.pre_skew_x) && bits(post2x2[0][0]) == bits(other.post2x2[0][0]) && bits(post2x2[0][1]) == bits(other.post2x2[0][1]) && bits(post2x2[1][0]) == bits(other.post2x2[1][0]) && bits(post2x2[1][1]) == bits(other.post2x2[1][1]) && foreground_color == other.foreground_color && flags == other.flags && mask_format == other.mask_format;
}

std::size_t ScalerContextRecHash::operator()(const ScalerContextRec& rec) const {
  const auto bits = [](float value) {
    return std::bit_cast<std::uint32_t>(value);
  };
  const std::array<std::uint32_t, 10> words = {
      rec.typeface_id,
      bits(rec.text_size),
      bits(rec.pre_scale_x),
      bits(rec.pre_skew_x),
      bits(rec.post2x2[0][0]),
      bits(rec.post2x2[0][1]),
      bits(rec.post2x2[1][0]),
      bits(rec.post2x2[1][1]),
      rec.foreground_color,
      rec.flags | (static_cast<std::uint32_t>(rec.mask_format) << 16)};
  return base::FastHash(base::as_bytes(base::span(words)));
}

ScalerContext::ScalerContext(std::shared_ptr<FontFace> typeface, const ScalerContextRec& rec)
    : rec_(PreprocessRec(typeface.get(), rec)),
      typeface_(std::move(typeface)) {
}

ScalerContext::~ScalerContext() = default;

ScalerContextRec ScalerContext::PreprocessRec(const FontFace* typeface, const ScalerContextRec& rec) {
  ScalerContextRec result = rec;

  // Allow the typeface to adjust the rec. SkEmptyTypeface does nothing. The
  // luminance adjustment that follows upstream only feeds mask pre-blending.
  if (typeface) FilterRecFreeType(&result);
  return result;
}

void ScalerContext::SaturateGlyphBounds(PlatformGlyph* glyph, ScalarRect&& r) {
  r.RoundOut();
  glyph->left_ = SaturateCast<std::int16_t>(r.left);
  glyph->top_ = SaturateCast<std::int16_t>(r.top);
  glyph->width_ = SaturateCast<std::uint16_t>(r.Width());
  glyph->height_ = SaturateCast<std::uint16_t>(r.Height());
}

PlatformGlyph ScalerContext::MakeGlyph(std::uint16_t glyph_id) {
  return InternalMakeGlyph(glyph_id, rec_.mask_format);
}

PlatformGlyph ScalerContext::InternalMakeGlyph(std::uint16_t glyph_id, MaskFormat format) {
  PlatformGlyph glyph{glyph_id};
  glyph.mask_format_ = format; // subclass may return a different value
  GlyphMetrics mx = GenerateMetrics(glyph);

  glyph.advance_x_ = mx.advance.x;
  glyph.advance_y_ = mx.advance.y;
  glyph.mask_format_ = mx.mask_format;
  glyph.scaler_context_bits_ = mx.extra_bits;

  // The internalGetPath branch is not ported: fGenerateImageFromPath
  // (fFrameWidth >= 0 or a path effect) is false for the default fill paint,
  // and the FreeType scaler never sets computeFromPath.
  SaturateGlyphBounds(&glyph, std::move(mx.bounds));

  // if either dimension is empty, zap the image bounds of the glyph
  if (0 == glyph.width_ || 0 == glyph.height_) {
    glyph.left_ = 0;
    glyph.top_ = 0;
    glyph.width_ = 0;
    glyph.height_ = 0;
  }
  return glyph;
}

void ScalerContext::MakeRecAndEffects(const PlatformFont& font, ScalerContextRec* rec) {
  *rec = ScalerContextRec();

  const FontFace* typeface = font.GetTypeface().get();

  rec->typeface_id = typeface ? typeface->UniqueId() : 0;
  rec->text_size = font.GetSize();
  rec->pre_scale_x = font.GetScaleX();
  rec->pre_skew_x = font.GetSkewX();

  // SkMatrix::I() has neither kScale_Mask nor kAffine_Mask.
  rec->post2x2[0][0] = rec->post2x2[1][1] = 1;
  rec->post2x2[0][1] = rec->post2x2[1][0] = 0;

  unsigned flags = 0;

  if (font.IsEmbolden()) {
    flags |= kEmbolden_Flag;
  }

  rec->mask_format = ComputeMaskFormat(font);

  if (MaskFormat::kLCD16 == rec->mask_format) {
    // too_big_for_lcd() and SkSurfaceProps()'s kUnknown_SkPixelGeometry
    // both end here.
    rec->mask_format = MaskFormat::kA8;
    flags |= kGenA8FromLCD_Flag;
  }

  if (font.IsEmbeddedBitmaps()) {
    flags |= kEmbeddedBitmapText_Flag;
  }
  if (font.IsSubpixel()) {
    flags |= kSubpixelPositioning_Flag;
  }
  if (font.IsForceAutoHinting()) {
    flags |= kForceAutohinting_Flag;
  }
  if (font.IsLinearMetrics()) {
    flags |= kLinearMetrics_Flag;
  }
  if (font.IsBaselineSnap()) {
    flags |= kBaselineSnap_Flag;
  }
  if (typeface && typeface->GlyphMaskNeedsCurrentColor()) {
    flags |= kNeedsForegroundColor_Flag;
    // SkPaint's default color, SK_ColorBLACK.
    rec->foreground_color = 0xFF000000u;
  }
  rec->flags = static_cast<std::uint16_t>(flags);

  // these modify flags, so do them after assigning flags
  rec->SetHinting(font.GetHinting());
}

std::unique_ptr<ScalerContext> ScalerContext::MakeEmpty(std::shared_ptr<FontFace> typeface, const ScalerContextRec& rec) {
  return std::make_unique<EmptyScalerContext>(std::move(typeface), rec);
}

std::unique_ptr<ScalerContext> CreateScalerContext(std::shared_ptr<FontFace> typeface, const ScalerContextRec& rec) {
  if (!typeface) return ScalerContext::MakeEmpty(nullptr, rec);
  return CreateScalerContextFreeType(std::move(typeface), rec);
}

} // namespace bkfont
