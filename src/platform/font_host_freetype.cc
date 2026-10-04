// Ported from: skia/src/ports/SkFontHost_FreeType.cpp

#include "font_host_freetype.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <utility>

#include <freetype/ftbitmap.h>
#include <freetype/ftcolor.h>
#include <freetype/ftlcdfil.h>
#include <freetype/ftmm.h>
#include <freetype/ftmodapi.h>
#include <freetype/ftoutln.h>
#include <freetype/ftsizes.h>

#include "base/immediate_crash.h"
#include "font_host_freetype_common.h"
#include "matrix.h"
#include "rect.h"
#include "scalar.h"
#include "scaler_context.h"

#ifdef TT_SUPPORT_COLRV1
// FT_ClipBox and FT_Get_Color_Glyph_ClipBox introduced VER-2-11-0-18-g47cf8ebf4
// FT_COLR_COMPOSITE_PLUS and renumbering introduced VER-2-11-0-21-ge40ae7569
// FT_SIZEOF_LONG_LONG introduced VER-2-11-0-31-gffdac8d67
// FT_PaintRadialGradient changed size and layout at VER-2-11-0-147-gd3d3ff76d
// FT_STATIC_CAST introduced VER-2-11-0-172-g9079c5d91
// So undefine TT_SUPPORT_COLRV1 before 2.11.1 but not if FT_STATIC_CAST is defined.
#if (((FREETYPE_MAJOR) < 2) || ((FREETYPE_MAJOR) == 2 && (FREETYPE_MINOR) < 11) || ((FREETYPE_MAJOR) == 2 && (FREETYPE_MINOR) == 11 && (FREETYPE_PATCH) < 1)) && !defined(FT_STATIC_CAST)
#undef TT_SUPPORT_COLRV1
#endif
#endif

// hand-tuned value to reduce outline embolden strength
#ifndef SK_OUTLINE_EMBOLDEN_DIVISOR
#define SK_OUTLINE_EMBOLDEN_DIVISOR 24
#endif

namespace bkfont {

namespace {

struct FTSizeDeleter {
  void operator()(FT_Size size) const {
    FT_Done_Size(size);
  }
};
using UniqueFTSize = std::unique_ptr<FT_SizeRec, FTSizeDeleter>;

extern "C" {
static void* ft_alloc(FT_Memory, long size) {
  // sk_malloc_canfail.
  return std::malloc(static_cast<std::size_t>(size));
}
static void ft_free(FT_Memory, void* block) {
  std::free(block);
}
static void* ft_realloc(FT_Memory, long, long new_size, void* block) {
  // sk_realloc_throw.
  if (new_size == 0) {
    std::free(block);
    return nullptr;
  }
  void* result = std::realloc(block, static_cast<std::size_t>(new_size));
  if (result == nullptr) base::ImmediateCrash();
  return result;
}
}

FT_MemoryRec_ ft_memory = {nullptr, ft_alloc, ft_free, ft_realloc};

class FreeTypeLibrary {
public:
  FreeTypeLibrary()
      : library_(nullptr) {
    if (FT_New_Library(&ft_memory, &library_)) {
      return;
    }
    FT_Add_Default_Modules(library_);
    FT_Set_Default_Properties(library_);

    // Subpixel anti-aliasing may be unfiltered until the LCD filter is set.
    // Newer versions may still need this, so this test with side effects must
    // come first. The default has changed over time, so this doesn't mean the
    // same thing to all users.
    FT_Library_SetLcdFilter(library_, FT_LCD_FILTER_DEFAULT);
  }
  ~FreeTypeLibrary() {
    if (library_) {
      FT_Done_Library(library_);
    }
  }
  FreeTypeLibrary(const FreeTypeLibrary&) = delete;
  FreeTypeLibrary& operator=(const FreeTypeLibrary&) = delete;

  FT_Library Library() {
    return library_;
  }

private:
  FT_Library library_;
};

FreeTypeLibrary* ft_library;

bool IsLcd(const ScalerContextRec& rec) {
  return MaskFormat::kLCD16 == rec.mask_format;
}

bool BothZero(float a, float b) {
  return 0 == a && 0 == b;
}

// returns false if there is any non-90-rotation or skew
bool IsAxisAligned(const ScalerContextRec& rec) {
  return 0 == rec.pre_skew_x && (BothZero(rec.post2x2[0][1], rec.post2x2[1][0]) || BothZero(rec.post2x2[0][0], rec.post2x2[1][1]));
}

FT_Int ChooseBitmapStrike(FT_Face face, FT_F26Dot6 scale_y) {
  if (face == nullptr) {
    return -1;
  }

  FT_Pos requested_ppem = scale_y; // FT_Bitmap_Size::y_ppem is in 26.6 format.
  FT_Int chosen_strike_index = -1;
  FT_Pos chosen_ppem = 0;
  for (FT_Int strike_index = 0; strike_index < face->num_fixed_sizes; ++strike_index) {
    FT_Pos strike_ppem = face->available_sizes[strike_index].y_ppem;
    if (strike_ppem == requested_ppem) {
      // exact match - our search stops here
      return strike_index;
    } else if (chosen_ppem < requested_ppem) {
      // attempt to increase chosen_ppem
      if (chosen_ppem < strike_ppem) {
        chosen_ppem = strike_ppem;
        chosen_strike_index = strike_index;
      }
    } else {
      // attempt to decrease chosen_ppem, but not below requested_ppem
      if (requested_ppem < strike_ppem && strike_ppem < chosen_ppem) {
        chosen_ppem = strike_ppem;
        chosen_strike_index = strike_index;
      }
    }
  }
  return chosen_strike_index;
}

constexpr std::uint32_t MakeTag(char a, char b, char c, char d) {
  return (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(b) << 16) | (static_cast<std::uint32_t>(c) << 8) | static_cast<std::uint32_t>(d);
}

class ScalerContextFreeType final : public ScalerContext {
public:
  ScalerContextFreeType(std::shared_ptr<FontFace> typeface, const ScalerContextRec& rec);
  ~ScalerContextFreeType() override;

  bool Success() const {
    return ft_size_ != nullptr && face_ != nullptr;
  }

protected:
  GlyphMetrics GenerateMetrics(const PlatformGlyph& glyph) override;

private:
  struct ScalerContextBits {
    static constexpr std::uint16_t kColrV0 = 1;
    static constexpr std::uint16_t kColrV1 = 2;
    static constexpr std::uint16_t kSvg = 3;
  };

  // See http://freetype.sourceforge.net/freetype2/docs/reference/ft2-bitmap_handling.html#FT_Bitmap_Embolden
  // This value was chosen by eyeballing the result in Firefox and trying to
  // match it.
  static constexpr FT_Pos kBitmapEmboldenStrength = 1 << 6;

  // Caller must lock FreeTypeMutex() before calling this function.
  FT_Error SetupSize();
  static bool GetBoundsOfCurrentOutlineGlyph(FT_GlyphSlot glyph, ScalarRect* bounds);
  void UpdateGlyphBoundsIfLcd(GlyphMetrics* mx);
  // Caller must lock FreeTypeMutex() before calling this function.
  // update FreeType2 glyph slot with glyph emboldened
  bool EmboldenIfNeeded(FT_Face face, FT_GlyphSlot glyph, std::uint16_t gid);

  FreeTypeFaceRec* face_rec_; // Borrowed face from the typeface's FaceRec.
  FT_Face face_;              // Borrowed face from face_rec_.
  FT_Size ft_size_;           // The size to apply to the face_.
  FT_Int strike_index_;       // The bitmap strike for the face_ (or -1 if none).

  ScalarMatrix matrix22_scalar_;
  FT_Matrix matrix22_;
  ScalarPoint scale_;
  FT_Int32 load_glyph_flags_;
  bool do_linear_metrics_;
  bool lcd_is_vert_;
};

ScalerContextFreeType::ScalerContextFreeType(std::shared_ptr<FontFace> typeface, const ScalerContextRec& rec)
    : ScalerContext(typeface, rec),
      face_rec_(nullptr),
      face_(nullptr),
      ft_size_(nullptr),
      strike_index_(-1),
      matrix22_(),
      load_glyph_flags_(0),
      do_linear_metrics_(false),
      lcd_is_vert_(false) {
  AutoMutexExclusive ac(FreeTypeMutex());
  face_rec_ = typeface->GetFaceRec();

  // load the font file
  if (nullptr == face_rec_) {
    return;
  }

  lcd_is_vert_ = (rec_.flags & kLCD_Vertical_Flag) != 0;

  // compute the flags we send to Load_Glyph
  bool linear_metrics = IsLinearMetrics();
  {
    FT_Int32 load_flags = FT_LOAD_DEFAULT;

    if (MaskFormat::kBW == rec_.mask_format) {
      // See http://code.google.com/p/chromium/issues/detail?id=43252#c24
      load_flags = FT_LOAD_TARGET_MONO;
      if (rec_.GetHinting() == FontHinting::kNone) {
        load_flags |= FT_LOAD_NO_HINTING;
        linear_metrics = true;
      }
    } else {
      switch (rec_.GetHinting()) {
        case FontHinting::kNone:
          load_flags = FT_LOAD_NO_HINTING;
          linear_metrics = true;
          break;
        case FontHinting::kSlight:
          load_flags = FT_LOAD_TARGET_LIGHT; // This implies FORCE_AUTOHINT
          linear_metrics = true;
          break;
        case FontHinting::kNormal:
          load_flags = FT_LOAD_TARGET_NORMAL;
          break;
        case FontHinting::kFull:
          load_flags = FT_LOAD_TARGET_NORMAL;
          if (IsLcd(rec_)) {
            if (lcd_is_vert_) {
              load_flags = FT_LOAD_TARGET_LCD_V;
            } else {
              load_flags = FT_LOAD_TARGET_LCD;
            }
          }
          break;
      }
    }

    if (rec_.flags & kForceAutohinting_Flag) {
      load_flags |= FT_LOAD_FORCE_AUTOHINT;
    }

    if ((rec_.flags & kEmbeddedBitmapText_Flag) == 0) {
      load_flags |= FT_LOAD_NO_BITMAP;
    }

    // Always using FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH to get correct
    // advances, as fontconfig and cairo do.
    // See http://code.google.com/p/skia/issues/detail?id=222.
    load_flags |= FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH;

    // SkScalerContext::isVertical() is always false, so FT_LOAD_VERTICAL_LAYOUT
    // is never requested.

    load_glyph_flags_ = load_flags;
  }

  UniqueFTSize ft_size([this]() -> FT_Size {
    FT_Size size;
    FT_Error err = FT_New_Size(face_rec_->face.get(), &size);
    if (err != 0) {
      return nullptr;
    }
    return size;
  }());
  if (nullptr == ft_size) {
    return;
  }

  FT_Error err = FT_Activate_Size(ft_size.get());
  if (err != 0) {
    return;
  }

  rec_.ComputeMatrices(&scale_, &matrix22_scalar_);
  FT_F26Dot6 scale_x = FloatToFDot6(scale_.x);
  FT_F26Dot6 scale_y = FloatToFDot6(scale_.y);

  if (FT_IS_SCALABLE(face_rec_->face)) {
    err = FT_Set_Char_Size(face_rec_->face.get(), scale_x, scale_y, 72, 72);
    if (err != 0) {
      return;
    }

    // Adjust the matrix to reflect the actually chosen scale.
    // FreeType currently does not allow requesting sizes less than 1, this
    // allow for scaling. Don't do this at all sizes as that will interfere
    // with hinting.
    if (scale_.x < 1 || scale_.y < 1) {
      float upem = face_rec_->face->units_per_EM;
      FT_Size_Metrics& ftmetrics = face_rec_->face->size->metrics;
      float x_ppem = upem * FixedToFloat(ftmetrics.x_scale) / 64.0f;
      float y_ppem = upem * FixedToFloat(ftmetrics.y_scale) / 64.0f;
      matrix22_scalar_.PreScale(scale_.x / x_ppem, scale_.y / y_ppem);
    }

    // FT_LOAD_COLOR with scalable fonts means allow SVG. It is only set when
    // SkGraphics::GetOpenTypeSVGDecoderFactory() returns a factory, and no
    // factory is installed here.
  } else if (FT_HAS_FIXED_SIZES(face_rec_->face)) {
    strike_index_ = ChooseBitmapStrike(face_rec_->face.get(), scale_y);
    if (strike_index_ == -1) {
      return;
    }

    err = FT_Select_Size(face_rec_->face.get(), strike_index_);
    if (err != 0) {
      strike_index_ = -1;
      return;
    }

    // Adjust the matrix to reflect the actually chosen scale.
    // It is likely that the ppem chosen was not the one requested, this
    // allows for scaling.
    matrix22_scalar_.PreScale(scale_.x / face_rec_->face->size->metrics.x_ppem, scale_.y / face_rec_->face->size->metrics.y_ppem);

    // FreeType does not provide linear metrics for bitmap fonts.
    linear_metrics = false;

    // FreeType documentation says:
    // FT_LOAD_NO_BITMAP -- Ignore bitmap strikes when loading.
    // Bitmap-only fonts ignore this flag.
    //
    // However, in FreeType 2.5.1 color bitmap only fonts do not ignore this
    // flag. Force this flag off for bitmap only fonts.
    load_glyph_flags_ &= ~FT_LOAD_NO_BITMAP;

    // Color bitmaps are supported.
    load_glyph_flags_ |= FT_LOAD_COLOR;
  } else {
    return;
  }

  matrix22_.xx = FloatToFixed(matrix22_scalar_.GetScaleX());
  matrix22_.xy = FloatToFixed(-matrix22_scalar_.GetSkewX());
  matrix22_.yx = FloatToFixed(-matrix22_scalar_.GetSkewY());
  matrix22_.yy = FloatToFixed(matrix22_scalar_.GetScaleY());

  ft_size_ = ft_size.release();
  face_ = face_rec_->face.get();
  do_linear_metrics_ = linear_metrics;
}

ScalerContextFreeType::~ScalerContextFreeType() {
  AutoMutexExclusive ac(FreeTypeMutex());

  if (ft_size_ != nullptr) {
    FT_Done_Size(ft_size_);
  }

  face_rec_ = nullptr;
}

// We call this before each use of the face_, since we may be sharing this face
// with other context (at different sizes).
FT_Error ScalerContextFreeType::SetupSize() {
  FT_Error err = FT_Activate_Size(ft_size_);
  if (err != 0) {
    return err;
  }
  FT_Set_Transform(face_, &matrix22_, nullptr);
  return 0;
}

bool ScalerContextFreeType::GetBoundsOfCurrentOutlineGlyph(FT_GlyphSlot glyph, ScalarRect* bounds) {
  if (glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
    return false;
  }
  if (0 == glyph->outline.n_contours) {
    return false;
  }

  FT_BBox bbox;
  FT_Outline_Get_CBox(&glyph->outline, &bbox);
  *bounds = ScalarRect::MakeLTRB(FDot6ToFloat(bbox.xMin), -FDot6ToFloat(bbox.yMax), FDot6ToFloat(bbox.xMax), -FDot6ToFloat(bbox.yMin));
  return true;
}

void ScalerContextFreeType::UpdateGlyphBoundsIfLcd(GlyphMetrics* mx) {
  if (mx->mask_format == MaskFormat::kLCD16 && !mx->bounds.IsEmpty()) {
    mx->bounds.RoundOut();
    if (lcd_is_vert_) {
      mx->bounds.bottom += 1;
      mx->bounds.top -= 1;
    } else {
      mx->bounds.right += 1;
      mx->bounds.left -= 1;
    }
  }
}

ScalerContext::GlyphMetrics ScalerContextFreeType::GenerateMetrics(const PlatformGlyph& glyph) {
  AutoMutexExclusive ac(FreeTypeMutex());

  GlyphMetrics mx(glyph.GetMaskFormat());

  if (SetupSize()) {
    return mx;
  }

  FT_Bool have_layers = false;
#ifdef FT_COLOR_H
  // See https://skbug.com/40044044, if the face isn't marked scalable then
  // paths cannot be loaded.
  if (FT_IS_SCALABLE(face_)) {
    ScalarRect bounds;
#ifdef TT_SUPPORT_COLRV1
    FT_OpaquePaint opaque_layer_paint{nullptr, 1};
    if (FT_Get_Color_Glyph_Paint(face_, glyph.GetGlyphID(), FT_COLOR_INCLUDE_ROOT_TRANSFORM, &opaque_layer_paint)) {
      have_layers = true;
      mx.extra_bits = ScalerContextBits::kColrV1;

      // COLRv1 optionally provides a ClipBox.
      FT_ClipBox clip_box;
      if (FT_Get_Color_Glyph_ClipBox(face_, glyph.GetGlyphID(), &clip_box)) {
        // Find bounding box of clip box corner points, needed when clipbox is
        // transformed.
        FT_BBox bbox;
        bbox.xMin = clip_box.bottom_left.x;
        bbox.xMax = clip_box.bottom_left.x;
        bbox.yMin = clip_box.bottom_left.y;
        bbox.yMax = clip_box.bottom_left.y;
        for (auto& corner : {clip_box.top_left, clip_box.top_right, clip_box.bottom_right}) {
          bbox.xMin = std::min(bbox.xMin, corner.x);
          bbox.yMin = std::min(bbox.yMin, corner.y);
          bbox.xMax = std::max(bbox.xMax, corner.x);
          bbox.yMax = std::max(bbox.yMax, corner.y);
        }
        bounds = ScalarRect::MakeLTRB(FDot6ToFloat(bbox.xMin), -FDot6ToFloat(bbox.yMax), FDot6ToFloat(bbox.xMax), -FDot6ToFloat(bbox.yMin));
      } else {
        // Traverse the glyph graph with a focus on measuring the required
        // bounding box. The call to ComputeColrV1GlyphBoundingBox may modify
        // the face. Reset the face to load the base glyph for metrics.
        if (!ScalerContextFTUtils::ComputeColrV1GlyphBoundingBox(face_, glyph.GetGlyphID(), &bounds) || SetupSize()) {
          return mx;
        }
      }
    }
#endif // TT_SUPPORT_COLRV1

    if (!have_layers) {
      FT_LayerIterator layer_iterator = {0, 0, nullptr};
      FT_UInt layer_glyph_index;
      FT_UInt layer_color_index;
      FT_Int32 flags = load_glyph_flags_;
      flags |= FT_LOAD_BITMAP_METRICS_ONLY; // Don't decode any bitmaps.
      flags |= FT_LOAD_NO_BITMAP;           // Ignore embedded bitmaps.
      flags &= ~FT_LOAD_RENDER;             // Don't scan convert.
      flags &= ~FT_LOAD_COLOR;              // Ignore SVG.
      // For COLRv0 compute the glyph bounding box from the union of layer
      // bounding boxes.
      while (FT_Get_Color_Glyph_Layer(face_, glyph.GetGlyphID(), &layer_glyph_index, &layer_color_index, &layer_iterator)) {
        have_layers = true;
        if (FT_Load_Glyph(face_, layer_glyph_index, flags)) {
          return mx;
        }

        ScalarRect current_bounds;
        if (GetBoundsOfCurrentOutlineGlyph(face_->glyph, &current_bounds)) {
          bounds.Join(current_bounds);
        }
      }
      if (have_layers) {
        mx.extra_bits = ScalerContextBits::kColrV0;
      }
    }

    if (have_layers) {
      mx.mask_format = MaskFormat::kARGB32;
      mx.never_request_path = true;
      // updateGlyphBoundsIfSubpixel: the glyph carries no subpixel offset on
      // the metrics path.
      mx.bounds = bounds;
    }
  }
#endif // FT_COLOR_H

  // Even if have_layers, the base glyph must be loaded to get the metrics.
  if (FT_Load_Glyph(face_, glyph.GetGlyphID(), load_glyph_flags_ | FT_LOAD_BITMAP_METRICS_ONLY)) {
    return mx;
  }

  if (!have_layers) {
    EmboldenIfNeeded(face_, face_->glyph, glyph.GetGlyphID());

    if (face_->glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
      GetBoundsOfCurrentOutlineGlyph(face_->glyph, &mx.bounds);
      // updateGlyphBoundsIfSubpixel: no subpixel offset on the metrics path.
      UpdateGlyphBoundsIfLcd(&mx);

    } else if (face_->glyph->format == FT_GLYPH_FORMAT_BITMAP) {
      mx.never_request_path = true;

      if (face_->glyph->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA) {
        mx.mask_format = MaskFormat::kARGB32;
      }

      mx.bounds = ScalarRect::MakeXYWH(static_cast<float>(face_->glyph->bitmap_left), -static_cast<float>(face_->glyph->bitmap_top), static_cast<float>(face_->glyph->bitmap.width), static_cast<float>(face_->glyph->bitmap.rows));
      matrix22_scalar_.MapRect(&mx.bounds);
      // updateGlyphBoundsIfSubpixel: shouldSubpixelBitmap needs a subpixel
      // offset, which the metrics path never has.

#if defined(FT_CONFIG_OPTION_SVG)
    } else if (face_->glyph->format == FT_GLYPH_FORMAT_SVG) {
      mx.extra_bits = ScalerContextBits::kSvg;
      mx.mask_format = MaskFormat::kARGB32;
      mx.never_request_path = true;

      // drawSVGGlyph returns false without an OpenType SVG decoder factory,
      // and none is installed here.
      return mx;
#endif // FT_CONFIG_OPTION_SVG

    } else {
      return mx;
    }
  }

  // SkScalerContext::isVertical() is always false.
  if (do_linear_metrics_) {
    const float advance_scalar = FixedToFloat(face_->glyph->linearHoriAdvance);
    mx.advance.x = matrix22_scalar_.GetScaleX() * advance_scalar;
    mx.advance.y = matrix22_scalar_.GetSkewY() * advance_scalar;
  } else {
    mx.advance.x = FDot6ToFloat(face_->glyph->advance.x);
    mx.advance.y = -FDot6ToFloat(face_->glyph->advance.y);
  }

  return mx;
}

bool ScalerContextFreeType::EmboldenIfNeeded(FT_Face face, FT_GlyphSlot glyph, std::uint16_t gid) {
  // check to see if the embolden bit is set
  if (0 == (rec_.flags & kEmbolden_Flag)) {
    return false;
  }

  if (glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
    const FT_Pos strength = FT_MulFix(face->units_per_EM, face->size->metrics.y_scale) / SK_OUTLINE_EMBOLDEN_DIVISOR;
    return 0 == FT_Outline_Embolden(&glyph->outline, strength);
  } else if (glyph->format == FT_GLYPH_FORMAT_BITMAP) {
    if (!face_->glyph->bitmap.buffer) {
      FT_Load_Glyph(face_, gid, load_glyph_flags_);
    }
    FT_GlyphSlot_Own_Bitmap(glyph);
    return 0 == FT_Bitmap_Embolden(glyph->library, &glyph->bitmap, kBitmapEmboldenStrength, 0);
  }
  return false;
}

} // namespace

Mutex& FreeTypeMutex() {
  static Mutex& mutex = *(new Mutex);
  return mutex;
}

int FreeTypeFaceRec::ft_count_;

void FreeTypeFaceRec::RefFreeTypeLibrary() {
  if (0 == ft_count_) {
    ft_library = new FreeTypeLibrary;
  }
  ++ft_count_;
}

void FreeTypeFaceRec::UnrefFreeTypeLibrary() {
  --ft_count_;
  if (0 == ft_count_) {
    delete ft_library;
  }
}

FreeTypeFaceRec::FreeTypeFaceRec(std::unique_ptr<FontFileStream> file_stream)
    : stream(std::move(file_stream)) {
  RefFreeTypeLibrary();
}

FreeTypeFaceRec::~FreeTypeFaceRec() {
  face.reset(); // Must release face before the library, the library frees existing faces.
  UnrefFreeTypeLibrary();
}

// SkFontData carries the design coordinates in FreeType axis order. They are
// rebuilt here from the face's coordinates as SkFontScanner_FreeType's
// computeAxisValues does: start from the default, then the last coordinate
// given for the axis, pinned to its range.
void FreeTypeFaceRec::SetupAxes(const FontFace& typeface) {
  if (!(face->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS)) {
    return;
  }

  FT_MM_Var* variations = nullptr;
  if (FT_Get_MM_Var(face.get(), &variations)) {
    return;
  }

  const Vector<PlatformFontVariationAxis> coordinates = typeface.VariationCoordinates();
  Vector<FT_Fixed> coords(variations->num_axis);
  for (FT_UInt i = 0; i < variations->num_axis; ++i) {
    const FT_Var_Axis& axis = variations->axis[i];
    const float axis_min = FixedToFloat(axis.minimum);
    const float axis_max = FixedToFloat(axis.maximum);
    coords[i] = axis.def;
    for (wtf_size_t j = coordinates.size(); j-- > 0;) {
      if (coordinates[j].tag == axis.tag) {
        coords[i] = FloatToFixed(std::max(axis_min, std::min(coordinates[j].value, axis_max))); // SkTPin
        break;
      }
    }
  }
  FT_Done_MM_Var(ft_library->Library(), variations);

  FT_Set_Var_Design_Coordinates(face.get(), coords.size(), coords.data());
}

std::unique_ptr<FreeTypeFaceRec> FreeTypeFaceRec::Make(const FontFace* typeface) {
  int ttc_index = 0;
  std::unique_ptr<FontFileStream> file_stream = typeface->OpenStream(&ttc_index);
  if (nullptr == file_stream) {
    return nullptr;
  }

  std::unique_ptr<FreeTypeFaceRec> rec(new FreeTypeFaceRec(std::move(file_stream)));

  FT_Open_Args args;
  std::memset(&args, 0, sizeof(args));
  const void* memory_base = rec->stream->GetMemoryBase();
  if (memory_base) {
    args.flags = FT_OPEN_MEMORY;
    args.memory_base = static_cast<const FT_Byte*>(memory_base);
    args.memory_size = static_cast<FT_Long>(rec->stream->GetLength());
  } else {
    // The FT_OPEN_STREAM branch (sk_ft_stream_io) is not ported.
    return nullptr;
  }

  {
    FT_Face raw_face;
    FT_Error err = FT_Open_Face(ft_library->Library(), &args, ttc_index, &raw_face);
    if (err) {
      return nullptr;
    }
    rec->face.reset(raw_face);
  }

  rec->SetupAxes(*typeface);

  // FreeType will set the charmap to the "most unicode" cmap if it exists.
  // If there are no unicode cmaps, the charmap is set to nullptr.
  // However, "symbol" cmaps should also be considered "fallback unicode" cmaps
  // because they are effectively private use area only (even if they aren't).
  // This is the last on the fallback list at
  // https://developer.apple.com/fonts/TrueType-Reference-Manual/RM06/Chap6cmap.html
  if (!rec->face->charmap) {
    FT_Select_Charmap(rec->face.get(), FT_ENCODING_MS_SYMBOL);
  }

  return rec;
}

// SkTypeface_FreeType::getFaceRec. Caller must lock FreeTypeMutex() before
// calling this function.
FreeTypeFaceRec* FontFace::GetFaceRec() const {
  face_rec_once_([this] {
    face_rec_ = FreeTypeFaceRec::Make(this);
  });
  return face_rec_.get();
}

// SkTypeface_FreeType::onGlyphMaskNeedsCurrentColor.
bool FontFace::GlyphMaskNeedsCurrentColor() const {
  glyph_masks_may_need_current_color_once_([this] {
    glyph_masks_may_need_current_color_ = HasTable(MakeTag('C', 'O', 'L', 'R'));
#if defined(FT_CONFIG_OPTION_SVG)
    glyph_masks_may_need_current_color_ |= HasTable(MakeTag('S', 'V', 'G', ' '));
#endif // FT_CONFIG_OPTION_SVG
  });
  return glyph_masks_may_need_current_color_;
}

void FilterRecFreeType(ScalerContextRec* rec) {
  // SK_USE_FREETYPE_EMBOLDEN is treated as defined on every platform, as
  // Chromium's SkUserConfig.h does for SK_BUILD_FOR_UNIX, so
  // useStrokeForFakeBold is not called.

  // BOGUS: http://code.google.com/p/chromium/issues/detail?id=121119
  // Cap the requested size as larger sizes give bogus values.
  // Remove when http://code.google.com/p/skia/issues/detail?id=554 is fixed.
  // Note that this also currently only protects against large text size
  // requests, the total matrix is not taken into account here.
  if (rec->text_size > static_cast<float>(1 << 14)) {
    rec->text_size = static_cast<float>(1 << 14);
  }

  FontHinting h = rec->GetHinting();
  if (FontHinting::kFull == h && !IsLcd(*rec)) {
    // collapse full->normal hinting if we're not doing LCD
    h = FontHinting::kNormal;
  }

  // rotated text looks bad with hinting, so we disable it as needed
  if (!IsAxisAligned(*rec)) {
    h = FontHinting::kNone;
  }
  rec->SetHinting(h);

  // ignorePreBlend only touches the omitted gamma fields.
}

std::unique_ptr<ScalerContext> CreateScalerContextFreeType(std::shared_ptr<FontFace> typeface, const ScalerContextRec& rec) {
  auto scaler_context = std::make_unique<ScalerContextFreeType>(typeface, rec);
  if (scaler_context->Success()) {
    return scaler_context;
  }
  return ScalerContext::MakeEmpty(std::move(typeface), rec);
}

} // namespace bkfont
