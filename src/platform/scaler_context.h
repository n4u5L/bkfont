// Ported from: skia/src/core/SkScalerContext.h

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "mask_gamma.h"
#include "matrix.h"
#include "path.h"
#include "platform_font.h"
#include "platform_font_metrics.h"
#include "platform_glyph.h"
#include "platform_paint.h"
#include "rect.h"
#include "surface_props.h"
#include "typeface.h"

namespace bkfont {

class Arena;
class Drawable;

// SkMaskGamma. The following typedef hides from the rest of the
// implementation the number of most significant bits to consider when
// creating mask gamma tables. Two bits per channel was chosen as a balance
// between fidelity (more bits) and cache sizes (fewer bits). Three bits per
// channel was chosen when #303942; (used by the Chrome UI) turned out too
// green.
using MaskGamma = TMaskGamma<3, 3, 3>;

// SkScalerContextFlags.
enum class ScalerContextFlags : std::uint32_t {
  kNone = 0,
  kFakeGamma = 1 << 0,
  kBoostContrast = 1 << 1,
  kFakeGammaAndBoostContrast = kFakeGamma | kBoostContrast,
};

inline bool HasScalerContextFlag(ScalerContextFlags flags, ScalerContextFlags flag) {
  return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(flag)) != 0;
}

// SkScalerContextRec, also the whole SkDescriptor key: there are never
// effects, since the paint has no path effect or mask filter. The stroke
// fields are left out because the fill paint fixes them (-1/0/0/0, no
// kFrameAndFill_Flag).
struct ScalerContextRec {
  std::uint32_t typeface_id = 0;
  float text_size = 0;
  float pre_scale_x = 0;
  float pre_skew_x = 0;
  float post2x2[2][2] = {};

  // This will be set if to the paint's foreground color if
  // kNeedsForegroundColor is set, which will usually be the case for COLRv0
  // and COLRv1 fonts. MakeRecAndEffects zeroes the whole rec first, so it is 0
  // otherwise.
  std::uint32_t foreground_color = 0;

private:
  // These describe the parameters to create (uniquely identify) the
  // pre-blend.
  std::uint32_t lum_bits_ = 0;
  std::uint8_t device_gamma_ = 0; // 2.6, (0.0, 4.0) gamma, 0.0 for sRGB
  std::uint8_t contrast_ = 0;     // 0.8+1, [0.0, 1.0] artificial contrast

  static constexpr float ExternalGammaFromInternal(std::uint8_t g) {
    return static_cast<float>(g) / (1 << 6);
  }
  static constexpr std::uint8_t InternalGammaFromExternal(float g) {
    return static_cast<std::uint8_t>(g * (1 << 6));
  }
  static constexpr float ExternalContrastFromInternal(std::uint8_t c) {
    return static_cast<float>(c) / ((1 << 8) - 1);
  }
  static constexpr std::uint8_t InternalContrastFromExternal(float c) {
    return static_cast<std::uint8_t>((c * ((1 << 8) - 1)) + 0.5f);
  }

public:
  void SetDeviceGamma(float g) {
    device_gamma_ = InternalGammaFromExternal(g);
  }

  void SetContrast(float c) {
    contrast_ = InternalContrastFromExternal(c);
  }

  // The caller must hold the mask gamma cache mutex, as GetMaskPreBlend does.
  static const MaskGamma& CachedMaskGamma(std::uint8_t contrast, std::uint8_t gamma);
  const MaskGamma& GetCachedMaskGamma() const {
    return CachedMaskGamma(contrast_, device_gamma_);
  }

  // Causes the luminance color to be ignored, and the paint and device gamma
  // to be effectively 1.0
  void IgnoreGamma() {
    SetLuminanceColor(0);
    SetDeviceGamma(1);
  }

  // Causes the luminance color and contrast to be ignored, and the paint and
  // device gamma to be effectively 1.0.
  void IgnorePreBlend() {
    IgnoreGamma();
    SetContrast(0);
  }

  MaskFormat mask_format = MaskFormat::kBW;
  std::uint16_t flags = 0;

  FontHinting GetHinting() const;
  void SetHinting(FontHinting hinting);

  ScalarMatrix GetMatrixFrom2x2() const;
  ScalarMatrix GetLocalMatrix() const;
  ScalarMatrix GetSingleMatrix() const;

  // computeMatrices(PreMatrixScale::kFull, s, sA), the only form the
  // FreeType scaler uses. Returns false if the matrix is singular.
  bool ComputeMatrices(ScalarPoint* s, ScalarMatrix* s_a) const;

  AxisAlignment ComputeAxisAlignmentForHText() const;

  MaskFormat GetFormat() const {
    return mask_format;
  }

  ColorARGB GetLuminanceColor() const {
    return lum_bits_;
  }

  // SetLuminanceColor forces the alpha to be 0xFF because the blitter that
  // draws the glyph will apply the alpha from the paint. Don't apply the alpha
  // twice.
  void SetLuminanceColor(ColorARGB c);

  // SkDescriptor equality compares bytes, so floats compare by bit pattern.
  bool operator==(const ScalerContextRec& other) const;

private:
  friend struct ScalerContextRecHash;
  friend class ScalerContext;
};

struct ScalerContextRecHash {
  std::size_t operator()(const ScalerContextRec& rec) const;
};

class ScalerContext {
public:
  enum Flags : std::uint16_t {
    kFrameAndFill_Flag = 0x0001,
    kUnused = 0x0002,
    kEmbeddedBitmapText_Flag = 0x0004,
    kEmbolden_Flag = 0x0008,
    kSubpixelPositioning_Flag = 0x0010,
    kForceAutohinting_Flag = 0x0020, // Use auto instead of bytcode hinting if hinting.

    // together, these two flags resulting in a two bit value which matches
    // up with the SkPaint::Hinting enum.
    kHinting_Shift = 7, // to shift into the other flags above
    kHintingBit1_Flag = 0x0080,
    kHintingBit2_Flag = 0x0100,

    // Pixel geometry information.
    // only meaningful if fMaskFormat is kLCD16
    kLCD_Vertical_Flag = 0x0200, // else Horizontal
    kLCD_BGROrder_Flag = 0x0400, // else RGB order

    // Generate A8 from LCD source (for GDI and CoreGraphics).
    // only meaningful if fMaskFormat is kA8
    kGenA8FromLCD_Flag = 0x0800, // could be 0x200 (bit meaning dependent on fMaskFormat)
    kLinearMetrics_Flag = 0x1000,
    kBaselineSnap_Flag = 0x2000,

    kNeedsForegroundColor_Flag = 0x4000,

    // computed values
    kHinting_Mask = kHintingBit1_Flag | kHintingBit2_Flag,
  };

  virtual ~ScalerContext();
  ScalerContext(const ScalerContext&) = delete;
  ScalerContext& operator=(const ScalerContext&) = delete;

  Typeface* GetTypeface() const {
    return typeface_.get();
  }

  MaskFormat GetMaskFormat() const {
    return rec_.mask_format;
  }

  bool IsSubpixel() const {
    return (rec_.flags & kSubpixelPositioning_Flag) != 0;
  }

  bool IsLinearMetrics() const {
    return (rec_.flags & kLinearMetrics_Flag) != 0;
  }

  PlatformGlyph MakeGlyph(PackedGlyphID packed_id, Arena* arena);
  void GetImage(const PlatformGlyph& glyph);
  void GetPath(PlatformGlyph& glyph, Arena* arena);
  std::shared_ptr<Drawable> GetDrawable(PlatformGlyph& glyph);
  void GetFontMetrics(PlatformFontMetrics* metrics);

  // Return the size in bytes of the associated gamma lookup table
  static std::size_t GetGammaLUTSize(float contrast, float device_gamma, int* width, int* height);

  // Get the associated gamma lookup table. The 'data' pointer must point to
  // pre-allocated memory, with size in bytes greater than or equal to the
  // return value of GetGammaLUTSize().
  //
  // If the lookup table hasn't been initialized (e.g., it's linear), this will
  // return false.
  static bool GetGammaLUTData(float contrast, float device_gamma, std::uint8_t* data);

  static void MakeRecAndEffects(const PlatformFont& font, const PlatformPaint& paint,
                                const SurfaceProps& surface_props,
                                ScalerContextFlags scaler_context_flags,
                                const ScalarMatrix& device_matrix,
                                ScalerContextRec* rec);

  // If we are creating rec and effects from a font only, then there is no
  // device around either.
  static void MakeRecAndEffectsFromFont(const PlatformFont& font, ScalerContextRec* rec) {
    PlatformPaint paint;
    MakeRecAndEffects(font, paint, SurfaceProps(), ScalerContextFlags::kNone, ScalarMatrix(), rec);
  }

  static std::unique_ptr<ScalerContext> MakeEmpty(std::shared_ptr<Typeface> typeface, const ScalerContextRec& rec);

  static MaskGamma::PreBlend GetMaskPreBlend(const ScalerContextRec& rec);

  const ScalerContextRec& GetRec() const {
    return rec_;
  }

  // Return the axis (if any) that the baseline for horizontal text should
  // land on. As an example, the identity matrix will return
  // AxisAlignment::kX.
  AxisAlignment ComputeAxisAlignmentForHText() const;

protected:
  ScalerContext(std::shared_ptr<Typeface> typeface, const ScalerContextRec& rec);

  const ScalerContextRec rec_;

  struct GeneratedPath {
    ScalarPath path;
    bool modified;
  };

  struct GlyphMetrics {
    explicit GlyphMetrics(MaskFormat format)
        : mask_format(format) {
    }

    ScalarPoint advance;
    ScalarRect bounds;
    MaskFormat mask_format;
    std::uint16_t extra_bits = 0;
    bool never_request_path = false;
    bool compute_from_path = false;
    std::optional<GeneratedPath> generated_path;
  };

  virtual GlyphMetrics GenerateMetrics(const PlatformGlyph& glyph, Arena* arena) = 0;

  static void GenerateMetricsFromPath(PlatformGlyph* glyph, const ScalarPath& path, MaskFormat format,
                                      bool vertical_lcd, bool a8_from_lcd, bool hairline);
  static void SaturateGlyphBounds(PlatformGlyph* glyph, ScalarRect&& r);

  // Generates the contents of image_buffer. When called, image_buffer will be
  // pointing to a pre-allocated, uninitialized region of memory of size
  // glyph.ImageSize(). This method may not change the glyph's mask format.
  //
  // Because glyph.ImageSize() will determine the size of the image,
  // GenerateMetrics will be called before GenerateImage.
  virtual void GenerateImage(const PlatformGlyph& glyph, void* image_buffer) = 0;

  // Return the glyph's outline, or if the glyph cannot be converted to one,
  // return {}. Does not apply subpixel positioning to the path.
  [[nodiscard]] virtual std::optional<GeneratedPath> GeneratePath(const PlatformGlyph& glyph) = 0;

  // Returns the drawable for the glyph (if any).
  //
  // The generated drawable will be lifetime scoped to the lifetime of this
  // scaler context. This means the drawable may refer to the scaler context
  // and associated font data.
  virtual std::shared_ptr<Drawable> GenerateDrawable(const PlatformGlyph& glyph); // TODO: = 0

  // Retrieves font metrics.
  virtual void GenerateFontMetrics(PlatformFontMetrics* metrics) = 0;

private:
  friend class ScalerContextProxy;

  static ScalerContextRec PreprocessRec(const Typeface& typeface, const ScalerContextRec& rec);

  void InternalGetPath(PlatformGlyph& glyph, Arena* arena, std::optional<GeneratedPath>&& generated_path);
  PlatformGlyph InternalMakeGlyph(PackedGlyphID packed_id, MaskFormat format, Arena* arena);

  // Keeps the typeface alive while the scaler context borrows its face.
  std::shared_ptr<Typeface> typeface_;

  // fGenerateImageFromPath is always false: the paint has no stroke and no
  // path effect, so the path rasterization of images is not ported.

protected:
  // MaskGamma::PreBlend converts linear masks to gamma correcting masks.
  // Visible to subclasses so that GenerateImage can apply the pre-blend
  // directly.
  const MaskGamma::PreBlend pre_blend_;
};

} // namespace bkfont
