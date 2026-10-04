// Ported from: skia/src/core/SkScalerContext.h

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "font_face.h"
#include "matrix.h"
#include "platform_font.h"
#include "platform_glyph.h"
#include "rect.h"

namespace bkfont {

// SkScalerContextRec, also the whole SkDescriptor key: MakeCanonicalized
// never adds effects. The stroke fields are left out because the default
// fill paint fixes them (-1/0/0/0, no kFrameAndFill_Flag). The luminance,
// device gamma, paint gamma and contrast fields are left out because the
// default paint and kFakeGammaAndBoostContrast fix them; they only feed mask
// pre-blending.
struct ScalerContextRec {
  std::uint32_t typeface_id = 0;
  float text_size = 0;
  float pre_scale_x = 0;
  float pre_skew_x = 0;
  float post2x2[2][2] = {};
  std::uint32_t foreground_color = 0;
  std::uint16_t flags = 0;
  MaskFormat mask_format = MaskFormat::kBW;

  FontHinting GetHinting() const;
  void SetHinting(FontHinting hinting);

  ScalarMatrix GetMatrixFrom2x2() const;
  ScalarMatrix GetLocalMatrix() const;
  ScalarMatrix GetSingleMatrix() const;

  // computeMatrices(PreMatrixScale::kFull, s, sA), the only form the
  // FreeType scaler uses. Returns false if the matrix is singular.
  bool ComputeMatrices(ScalarPoint* s, ScalarMatrix* s_a) const;

  // SkDescriptor equality compares bytes, so floats compare by bit pattern.
  bool operator==(const ScalerContextRec& other) const;
};

struct ScalerContextRecHash {
  std::size_t operator()(const ScalerContextRec& rec) const;
};

class ScalerContext {
public:
  enum Flags : std::uint16_t {
    kFrameAndFill_Flag = 0x0001,
    kEmbeddedBitmapText_Flag = 0x0004,
    kEmbolden_Flag = 0x0008,
    kSubpixelPositioning_Flag = 0x0010,
    kForceAutohinting_Flag = 0x0020,

    // Together, these two flags resulting in a two bit value which matches
    // up with the SkFontHinting enum.
    kHinting_Shift = 7,
    kHintingBit1_Flag = 0x0080,
    kHintingBit2_Flag = 0x0100,

    // Pixel geometry information.
    // Only meaningful if fMaskFormat is kLCD16.
    kLCD_Vertical_Flag = 0x0200,
    kLCD_BGROrder_Flag = 0x0400,

    // Generate A8 from LCD source (for GDI and CoreGraphics).
    // Only meaningful if fMaskFormat is kA8.
    kGenA8FromLCD_Flag = 0x0800,
    kLinearMetrics_Flag = 0x1000,
    kBaselineSnap_Flag = 0x2000,

    kNeedsForegroundColor_Flag = 0x4000,

    // These flags are masks for the hinting bits.
    kHinting_Mask = kHintingBit1_Flag | kHintingBit2_Flag,
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
  };

  virtual ~ScalerContext();
  ScalerContext(const ScalerContext&) = delete;
  ScalerContext& operator=(const ScalerContext&) = delete;

  bool IsSubpixel() const {
    return (rec_.flags & kSubpixelPositioning_Flag) != 0;
  }
  bool IsLinearMetrics() const {
    return (rec_.flags & kLinearMetrics_Flag) != 0;
  }

  PlatformGlyph MakeGlyph(std::uint16_t glyph_id);

  const ScalerContextRec& GetRec() const {
    return rec_;
  }

  // MakeRecAndEffects(font, SkPaint(), SkSurfaceProps(),
  // kFakeGammaAndBoostContrast, SkMatrix::I(), rec, effects): the only
  // arguments MakeCanonicalized passes.
  static void MakeRecAndEffects(const PlatformFont& font, ScalerContextRec* rec);

  static std::unique_ptr<ScalerContext> MakeEmpty(std::shared_ptr<FontFace> typeface, const ScalerContextRec& rec);

protected:
  ScalerContext(std::shared_ptr<FontFace> typeface, const ScalerContextRec& rec);

  virtual GlyphMetrics GenerateMetrics(const PlatformGlyph& glyph) = 0;

  ScalerContextRec rec_;

private:
  static ScalerContextRec PreprocessRec(const FontFace* typeface, const ScalerContextRec& rec);
  static void SaturateGlyphBounds(PlatformGlyph* glyph, ScalarRect&& r);

  PlatformGlyph InternalMakeGlyph(std::uint16_t glyph_id, MaskFormat format);

  // Keeps the typeface alive while the scaler context borrows its face.
  std::shared_ptr<FontFace> typeface_;
};

// SkTypeface::createScalerContext. A null typeface is SkEmptyTypeface.
std::unique_ptr<ScalerContext> CreateScalerContext(std::shared_ptr<FontFace> typeface, const ScalerContextRec& rec);

} // namespace bkfont
