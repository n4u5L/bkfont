// Ported from: skia/src/core/SkScalerContext.cpp

#include "scaler_context.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

#include "arena.h"
#include "base/hash/hash.h"
#include "base/mutex.h"
#include "picture.h"
#include "scalar.h"
#include "shader.h"

namespace bkfont {

namespace {

// Return the closest D for the given S. Returns
// std::numeric_limits<D>::max() for NaN.
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

// Return the scalar with only limited fractional precision. Used to
// consolidate matrices that vary only slightly when we create our key into
// the font cache, since the font scaler typically returns the same looking
// resuts for tiny changes in the matrix.
float Relax(float x) {
  // SkScalarRoundToScalar rounds in double.
  const float n = static_cast<float>(std::floor(static_cast<double>(x * 1024) + 0.5));
  return n / 1024.0f;
}

// Beyond this size, LCD doesn't appreciably improve quality, but it always
// cost more RAM and draws slower, so we set a cap.
constexpr float kMaxSizeForLCDText = 48;

constexpr float kMaxSize2ForLCDText = kMaxSizeForLCDText * kMaxSizeForLCDText;

bool TooBigForLCD(const ScalerContextRec& rec, bool check_post2x2) {
  if (check_post2x2) {
    float area = rec.post2x2[0][0] * rec.post2x2[1][1] - rec.post2x2[1][0] * rec.post2x2[0][1];
    area *= rec.text_size * rec.text_size;
    return area > kMaxSize2ForLCDText;
  } else {
    return rec.text_size > kMaxSizeForLCDText;
  }
}

// In order to call CachedMaskGamma the caller must hold the
// MaskGammaCacheMutex and continue to hold it until the returned reference is
// turned into a pre-blend.
Mutex& MaskGammaCacheMutex() {
  static Mutex& mutex = *(new Mutex);
  return mutex;
}

const MaskGamma& LinearGamma() {
  static const std::shared_ptr<const MaskGamma>& linear = *new std::shared_ptr<const MaskGamma>(std::make_shared<MaskGamma>());
  return *linear;
}

// The pre-blends hold references, so a replaced gamma stays alive while used.
std::shared_ptr<const MaskGamma> g_default_mask_gamma;
std::shared_ptr<const MaskGamma> g_mask_gamma;
std::uint8_t g_contrast = 0;
std::uint8_t g_gamma = 0;

class EmptyScalerContext final : public ScalerContext {
public:
  EmptyScalerContext(std::shared_ptr<Typeface> typeface, const ScalerContextRec& rec)
      : ScalerContext(std::move(typeface), rec) {
  }

protected:
  GlyphMetrics GenerateMetrics(const PlatformGlyph& glyph, Arena*) override {
    return GlyphMetrics(glyph.GetMaskFormat());
  }
  void GenerateImage(const PlatformGlyph&, void*) override {
  }
  std::optional<GeneratedPath> GeneratePath(const PlatformGlyph&) override {
    return {};
  }
  void GenerateFontMetrics(PlatformFontMetrics* metrics) override {
    if (metrics) {
      *metrics = PlatformFontMetrics();
    }
  }
};

} // namespace

// -- ScalerContextRec ---------------------------------------------------------

const MaskGamma& ScalerContextRec::CachedMaskGamma(std::uint8_t contrast, std::uint8_t gamma) {
  constexpr std::uint8_t kContrast0 = InternalContrastFromExternal(0);
  constexpr std::uint8_t kGamma1 = InternalGammaFromExternal(1);
  if (kContrast0 == contrast && kGamma1 == gamma) {
    return LinearGamma();
  }
  constexpr std::uint8_t kDefaultContrast = InternalContrastFromExternal(kGammaContrast);
  constexpr std::uint8_t kDefaultGamma = InternalGammaFromExternal(kGammaExponent);
  if (kDefaultContrast == contrast && kDefaultGamma == gamma) {
    if (!g_default_mask_gamma) {
      g_default_mask_gamma = std::make_shared<MaskGamma>(ExternalContrastFromInternal(contrast),
                                                               ExternalGammaFromInternal(gamma));
    }
    return *g_default_mask_gamma;
  }
  if (!g_mask_gamma || g_contrast != contrast || g_gamma != gamma) {
    g_mask_gamma = std::make_shared<MaskGamma>(ExternalContrastFromInternal(contrast),
                                                     ExternalGammaFromInternal(gamma));
    g_contrast = contrast;
    g_gamma = gamma;
  }
  return *g_mask_gamma;
}

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
  // size, so use the matricies to zero. If one of the scale factors is less
  // than 1/256 then an EM filling square will never affect any pixels. If
  // there are any nonfinite numbers in the matrix, bail out and set the
  // matrices to zero.
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

AxisAlignment ScalerContextRec::ComputeAxisAlignmentForHText() const {
  // Why post2x2 can be used here.
  // GetSingleMatrix multiplies in GetLocalMatrix, which consists of
  // * text_size (a scale, which has no effect)
  // * pre_scale_x (a scale in x, which has no effect)
  // * pre_skew_x (has no effect, but would on vertical text alignment).
  // In other words, making the text bigger, stretching it along the
  // horizontal axis, or fake italicizing it does not move the baseline.
  if ((flags & ScalerContext::kBaselineSnap_Flag) == 0) {
    return AxisAlignment::kNone;
  }

  if (0 == post2x2[1][0]) {
    // The x axis is mapped onto the x axis.
    return AxisAlignment::kX;
  }
  if (0 == post2x2[0][0]) {
    // The x axis is mapped onto the y axis.
    return AxisAlignment::kY;
  }
  return AxisAlignment::kNone;
}

void ScalerContextRec::SetLuminanceColor(ColorARGB c) {
  lum_bits_ = MaskGamma::CanonicalColor(ColorSetRGB(ColorGetR(c), ColorGetG(c), ColorGetB(c)));
}

bool ScalerContextRec::operator==(const ScalerContextRec& other) const {
  const auto bits = [](float value) {
    return std::bit_cast<std::uint32_t>(value);
  };
  return typeface_id == other.typeface_id && bits(text_size) == bits(other.text_size) && bits(pre_scale_x) == bits(other.pre_scale_x) && bits(pre_skew_x) == bits(other.pre_skew_x) && bits(post2x2[0][0]) == bits(other.post2x2[0][0]) && bits(post2x2[0][1]) == bits(other.post2x2[0][1]) && bits(post2x2[1][0]) == bits(other.post2x2[1][0]) && bits(post2x2[1][1]) == bits(other.post2x2[1][1]) && foreground_color == other.foreground_color && lum_bits_ == other.lum_bits_ && device_gamma_ == other.device_gamma_ && contrast_ == other.contrast_ && mask_format == other.mask_format && flags == other.flags;
}

std::size_t ScalerContextRecHash::operator()(const ScalerContextRec& rec) const {
  const auto bits = [](float value) {
    return std::bit_cast<std::uint32_t>(value);
  };
  const std::array<std::uint32_t, 12> words = {
      rec.typeface_id,
      bits(rec.text_size),
      bits(rec.pre_scale_x),
      bits(rec.pre_skew_x),
      bits(rec.post2x2[0][0]),
      bits(rec.post2x2[0][1]),
      bits(rec.post2x2[1][0]),
      bits(rec.post2x2[1][1]),
      rec.foreground_color,
      rec.lum_bits_,
      rec.device_gamma_ | (static_cast<std::uint32_t>(rec.contrast_) << 8),
      rec.flags | (static_cast<std::uint32_t>(rec.mask_format) << 16)};
  return base::FastHash(base::as_bytes(base::span(words)));
}

// -- ScalerContext ------------------------------------------------------------

ScalerContext::ScalerContext(std::shared_ptr<Typeface> typeface, const ScalerContextRec& rec)
    : rec_(PreprocessRec(*typeface, rec)),
      typeface_(std::move(typeface)),
      pre_blend_(GetMaskPreBlend(rec_)) {
}

ScalerContext::~ScalerContext() = default;

ScalerContextRec ScalerContext::PreprocessRec(const Typeface& typeface, const ScalerContextRec& rec) {
  ScalerContextRec result = rec;

  // Allow the typeface to adjust the rec.
  typeface.FilterRec(&result);

  // The mask filter branch, which ignores the pre-blend, is not ported.

  ColorARGB lum_color = result.GetLuminanceColor();

  if (result.mask_format == MaskFormat::kA8) {
    unsigned lum = ComputeLuminance(ColorGetR(lum_color), ColorGetG(lum_color), ColorGetB(lum_color));
    lum_color = ColorSetRGB(lum, lum, lum);
  }

  // TODO: remove CanonicalColor when we to fix up Chrome layout tests.
  result.SetLuminanceColor(lum_color);

  return result;
}

MaskGamma::PreBlend ScalerContext::GetMaskPreBlend(const ScalerContextRec& rec) {
  AutoMutexExclusive ama(MaskGammaCacheMutex());

  const MaskGamma& mask_gamma = rec.GetCachedMaskGamma();

  // TODO: remove CanonicalColor when we to fix up Chrome layout tests.
  return mask_gamma.MakePreBlend(rec.GetLuminanceColor());
}

std::size_t ScalerContext::GetGammaLUTSize(float contrast, float device_gamma, int* width, int* height) {
  AutoMutexExclusive ama(MaskGammaCacheMutex());
  const MaskGamma& mask_gamma = ScalerContextRec::CachedMaskGamma(
      ScalerContextRec::InternalContrastFromExternal(contrast),
      ScalerContextRec::InternalGammaFromExternal(device_gamma));
  mask_gamma.GetGammaTableDimensions(width, height);
  return mask_gamma.GetGammaTableSizeInBytes();
}

bool ScalerContext::GetGammaLUTData(float contrast, float device_gamma, std::uint8_t* data) {
  AutoMutexExclusive ama(MaskGammaCacheMutex());
  const MaskGamma& mask_gamma = ScalerContextRec::CachedMaskGamma(
      ScalerContextRec::InternalContrastFromExternal(contrast),
      ScalerContextRec::InternalGammaFromExternal(device_gamma));
  const std::uint8_t* gamma_tables = mask_gamma.GetGammaTables();
  if (!gamma_tables) {
    return false;
  }

  std::memcpy(data, gamma_tables, mask_gamma.GetGammaTableSizeInBytes());
  return true;
}

PlatformGlyph ScalerContext::MakeGlyph(PackedGlyphID packed_id, Arena* arena) {
  return InternalMakeGlyph(packed_id, rec_.mask_format, arena);
}

void ScalerContext::SaturateGlyphBounds(PlatformGlyph* glyph, ScalarRect&& r) {
  r.RoundOut();
  glyph->left_ = SaturateCast<std::int16_t>(r.left);
  glyph->top_ = SaturateCast<std::int16_t>(r.top);
  glyph->width_ = SaturateCast<std::uint16_t>(r.Width());
  glyph->height_ = SaturateCast<std::uint16_t>(r.Height());
}

void ScalerContext::GenerateMetricsFromPath(PlatformGlyph* glyph, const ScalarPath& dev_path, MaskFormat,
                                            const bool vertical_lcd, const bool a8_from_lcd, const bool hairline) {
  // Only BW, A8, and LCD16 can be produced from paths.
  if (glyph->mask_format_ != MaskFormat::kBW &&
      glyph->mask_format_ != MaskFormat::kA8 &&
      glyph->mask_format_ != MaskFormat::kLCD16) {
    glyph->mask_format_ = MaskFormat::kA8;
  }

  ScalarRect bounds = dev_path.GetBounds();
  if (!bounds.IsEmpty()) {
    const bool from_lcd = (glyph->mask_format_ == MaskFormat::kLCD16) ||
                          (glyph->mask_format_ == MaskFormat::kA8 && a8_from_lcd);

    const bool need_extra_width = (from_lcd && !vertical_lcd) || hairline;
    const bool need_extra_height = (from_lcd && vertical_lcd) || hairline;
    if (need_extra_width) {
      bounds.RoundOut();
      bounds.Outset(1, 0);
    }
    if (need_extra_height) {
      bounds.RoundOut();
      bounds.Outset(0, 1);
    }
  }
  SaturateGlyphBounds(glyph, std::move(bounds));
}

PlatformGlyph ScalerContext::InternalMakeGlyph(PackedGlyphID packed_id, MaskFormat format, Arena* arena) {
  auto zero_bounds = [](PlatformGlyph& glyph) {
    glyph.left_ = 0;
    glyph.top_ = 0;
    glyph.width_ = 0;
    glyph.height_ = 0;
  };

  PlatformGlyph glyph{packed_id};
  glyph.mask_format_ = format; // subclass may return a different value
  GlyphMetrics mx = GenerateMetrics(glyph, arena);

  glyph.advance_x_ = mx.advance.x;
  glyph.advance_y_ = mx.advance.y;
  glyph.mask_format_ = mx.mask_format;
  glyph.scaler_context_bits_ = mx.extra_bits;

  // fGenerateImageFromPath is false, so only compute_from_path requests the
  // path here.
  if (mx.compute_from_path) {
    InternalGetPath(glyph, arena, std::move(mx.generated_path));
    const ScalarPath* dev_path = glyph.Path();
    if (dev_path) {
      const bool do_vert = (rec_.flags & kLCD_Vertical_Flag) != 0;
      const bool a8_lcd = (rec_.flags & kGenA8FromLCD_Flag) != 0;
      const bool hairline = glyph.PathIsHairline();
      GenerateMetricsFromPath(&glyph, *dev_path, format, do_vert, a8_lcd, hairline);
    }
  } else {
    SaturateGlyphBounds(&glyph, std::move(mx.bounds));
    if (mx.never_request_path) {
      glyph.SetPath(arena, nullptr, false, false);
    }
  }

  // if either dimension is empty, zap the image bounds of the glyph
  if (0 == glyph.width_ || 0 == glyph.height_) {
    zero_bounds(glyph);
    return glyph;
  }

  // The mask filter bounds are not ported.
  return glyph;
}

void ScalerContext::GetImage(const PlatformGlyph& orig_glyph) {
  // Without a mask filter the unfiltered glyph is the original glyph, and
  // without fGenerateImageFromPath the image is always generated directly.
  GenerateImage(orig_glyph, orig_glyph.image_);
}

void ScalerContext::GetPath(PlatformGlyph& glyph, Arena* arena) {
  InternalGetPath(glyph, arena, std::nullopt);
}

std::shared_ptr<Drawable> ScalerContext::GetDrawable(PlatformGlyph& glyph) {
  return GenerateDrawable(glyph);
}

// TODO: make pure virtual
std::shared_ptr<Drawable> ScalerContext::GenerateDrawable(const PlatformGlyph&) {
  return nullptr;
}

void ScalerContext::GetFontMetrics(PlatformFontMetrics* fm) {
  GenerateFontMetrics(fm);
}

void ScalerContext::InternalGetPath(PlatformGlyph& glyph, Arena* arena, std::optional<GeneratedPath>&& generated_path) {
  if (glyph.SetPathHasBeenCalled()) {
    return;
  }

  if (!generated_path) {
    generated_path = GeneratePath(glyph);
  }
  if (!generated_path) {
    glyph.SetPath(arena, static_cast<const ScalarPath*>(nullptr), false, false);
    return;
  }

  ScalarPath path = std::move(generated_path->path);
  bool path_modified = generated_path->modified;

  if (rec_.flags & kSubpixelPositioning_Flag) {
    PackedGlyphID glyph_id = glyph.GetPackedID();
    Fixed dx = glyph_id.GetSubXFixed();
    Fixed dy = glyph_id.GetSubYFixed();
    if (dx | dy) {
      path_modified = true;
      path = path.MakeOffset(FixedToFloat(dx), FixedToFloat(dy));
    }
  }

  // The frame width is negative and there is no path effect, so the stroke
  // and path effect stages are never reached.
  glyph.SetPath(arena, &path, false, path_modified);
}

AxisAlignment ScalerContext::ComputeAxisAlignmentForHText() const {
  return rec_.ComputeAxisAlignmentForHText();
}

void ScalerContext::MakeRecAndEffects(const PlatformFont& font, const PlatformPaint& paint,
                                      const SurfaceProps& surface_props,
                                      ScalerContextFlags scaler_context_flags,
                                      const ScalarMatrix& device_matrix,
                                      ScalerContextRec* rec) {
  // sk_bzero.
  *rec = ScalerContextRec();

  const Typeface* typeface = font.GetTypeface().get();

  rec->typeface_id = typeface->UniqueID();
  rec->text_size = font.GetSize();
  rec->pre_scale_x = font.GetScaleX();
  rec->pre_skew_x = font.GetSkewX();

  bool check_post2x2 = false;

  const unsigned mask = device_matrix.GetType();
  if (mask & ScalarMatrix::kScale_Mask) {
    rec->post2x2[0][0] = Relax(device_matrix.GetScaleX());
    rec->post2x2[1][1] = Relax(device_matrix.GetScaleY());
    check_post2x2 = true;
  } else {
    rec->post2x2[0][0] = rec->post2x2[1][1] = 1;
  }
  if (mask & ScalarMatrix::kAffine_Mask) {
    rec->post2x2[0][1] = Relax(device_matrix.GetSkewX());
    rec->post2x2[1][0] = Relax(device_matrix.GetSkewY());
    check_post2x2 = true;
  } else {
    rec->post2x2[0][1] = rec->post2x2[1][0] = 0;
  }

  unsigned flags = 0;

  if (font.IsEmbolden()) {
    flags |= kEmbolden_Flag;
  }

  // The fill paint leaves the frame width at -1 and the stroke fields at 0.

  rec->mask_format = ComputeMaskFormat(font);

  if (MaskFormat::kLCD16 == rec->mask_format) {
    if (TooBigForLCD(*rec, check_post2x2)) {
      rec->mask_format = MaskFormat::kA8;
      flags |= kGenA8FromLCD_Flag;
    } else {
      PixelGeometry geometry = surface_props.GetPixelGeometry();

      switch (geometry) {
      case PixelGeometry::kUnknown:
        // eeek, can't support LCD
        rec->mask_format = MaskFormat::kA8;
        flags |= kGenA8FromLCD_Flag;
        break;
      case PixelGeometry::kRGB_H:
        // our default, do nothing.
        break;
      case PixelGeometry::kBGR_H:
        flags |= kLCD_BGROrder_Flag;
        break;
      case PixelGeometry::kRGB_V:
        flags |= kLCD_Vertical_Flag;
        break;
      case PixelGeometry::kBGR_V:
        flags |= kLCD_Vertical_Flag;
        flags |= kLCD_BGROrder_Flag;
        break;
      }
    }
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
  if (typeface->GlyphMaskNeedsCurrentColor()) {
    flags |= kNeedsForegroundColor_Flag;
    rec->foreground_color = paint.GetColor();
  }
  rec->flags = static_cast<std::uint16_t>(flags);

  // these modify flags, so do them after assigning flags
  rec->SetHinting(font.GetHinting());
  // SkPaintPriv::ComputeLuminanceColor, without color filters.
  Color4f luminance_color = paint.GetColor4f();
  if (const auto& shader = paint.GetShader(); shader && !shader->AsLuminanceColor(&luminance_color)) {
    luminance_color = {0.5f, 0.5f, 0.5f, 1.0f};
  }
  rec->SetLuminanceColor(luminance_color.ToColor());

  // The paint color is always converted to the device colr space, so the
  // paint gamma is now always equal to the device gamma. The math in
  // MaskGamma can handle them being different, but it requires superluminous
  // masks when Ex : deviceGamma(x) < paintGamma(x) and x is sufficiently
  // large.
  rec->SetDeviceGamma(surface_props.TextGamma());
  rec->SetContrast(surface_props.TextContrast());

  if (!HasScalerContextFlag(scaler_context_flags, ScalerContextFlags::kFakeGamma)) {
    rec->IgnoreGamma();
  }
  if (!HasScalerContextFlag(scaler_context_flags, ScalerContextFlags::kBoostContrast)) {
    rec->SetContrast(0);
  }
}

std::unique_ptr<ScalerContext> ScalerContext::MakeEmpty(std::shared_ptr<Typeface> typeface, const ScalerContextRec& rec) {
  return std::make_unique<EmptyScalerContext>(std::move(typeface), rec);
}

} // namespace bkfont
