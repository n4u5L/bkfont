// Ported from: skia/src/ports/SkFontHost_FreeType.cpp

#include "typeface_freetype.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <utility>

#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftbitmap.h>
#include <freetype/ftcolor.h>
#include <freetype/ftlcdfil.h>
#include <freetype/ftmm.h>
#include <freetype/ftmodapi.h>
#include <freetype/ftoutln.h>
#include <freetype/ftsizes.h>
#include <freetype/t1tables.h>
#include <freetype/tttables.h>

#include "base/immediate_crash.h"
#include "base/vector.h"
#include "canvas.h"
#include "font_host_freetype_common.h"
#include "opentype_svg_decoder.h"
#include "picture.h"
#include "pixmap.h"
#include "raster_canvas.h"
#include "matrix.h"
#include "ot_utils.h"
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

// SkFontScanner::AxisDefinitions and SkFontScanner::VariationPosition.
using AxisDefinitions = Vector<FontParameters::Variation::Axis, 4>;
using VariationPositionStorage = Vector<FontArguments::VariationPosition::Coordinate, 4>;

struct FreeDeleter {
  void operator()(void* p) const {
    // sk_free. FreeType allocates through ft_alloc below.
    std::free(p);
  }
};
// UniqueVoidPtr.
using UniqueVoidPtr = std::unique_ptr<void, FreeDeleter>;

struct FreeTypeFaceDeleter {
  void operator()(FT_Face face) const {
    FT_Done_Face(face);
  }
};
// SkUniqueFTFace.
using UniqueFTFace = std::unique_ptr<FT_FaceRec, FreeTypeFaceDeleter>;

struct FTSizeDeleter {
  void operator()(FT_Size size) const {
    FT_Done_Size(size);
  }
};
using UniqueFTSize = std::unique_ptr<FT_SizeRec, FTSizeDeleter>;

constexpr std::uint32_t MakeTag(char a, char b, char c, char d) {
  return (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(b) << 16) | (static_cast<std::uint32_t>(c) << 8) | static_cast<std::uint32_t>(d);
}

bool IsLcd(const ScalerContextRec& rec) {
  return MaskFormat::kLCD16 == rec.mask_format;
}

// SkFT_FixedToScalar.
float FTFixedToScalar(FT_Fixed x) {
  return FixedToFloat(x);
}

// SkTPin.
float Pin(float x, float lo, float hi) {
  return std::max(lo, std::min(x, hi));
}

// SkFixedRoundToInt.
int FixedRoundToInt(std::int32_t x) {
  return (x + 0x8000) >> 16;
}

bool GetAxes(FT_Face face, AxisDefinitions* axes) {
  if (face->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS) {
    FT_MM_Var* variations = nullptr;
    FT_Error err = FT_Get_MM_Var(face, &variations);
    if (err) {
      return false;
    }
    UniqueVoidPtr auto_free_variations(variations);

    axes->resize(variations->num_axis);
    for (FT_UInt i = 0; i < variations->num_axis; ++i) {
      const FT_Var_Axis& ft_axis = variations->axis[i];
      (*axes)[i].tag = static_cast<std::uint32_t>(ft_axis.tag);
      (*axes)[i].min = FTFixedToScalar(ft_axis.minimum);
      (*axes)[i].def = FTFixedToScalar(ft_axis.def);
      (*axes)[i].max = FTFixedToScalar(ft_axis.maximum);
    }
  }
  return true;
}

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

extern "C" {
static unsigned long sk_ft_stream_io(FT_Stream ft_stream,
                                     unsigned long offset,
                                     unsigned char* buffer,
                                     unsigned long count) {
  StreamAsset* stream = static_cast<StreamAsset*>(ft_stream->descriptor.pointer);

  if (count) {
    if (!stream->Seek(offset)) {
      return 0;
    }
    count = static_cast<unsigned long>(stream->Read(buffer, count));
  }
  return count;
}

static void sk_ft_stream_close(FT_Stream) {
}
}

// Copy the design variation coordinates into 'coordinates'.
//
// @param coordinates the buffer into which to write the design variation
// coordinates.
//
// @return The number of axes, or -1 if there is an error. If 'coordinates'
// has at least numAxes entries then it will be filled with the variation
// coordinates describing the position of this typeface in design variation
// space. It is possible the number of axes can be retrieved but actual
// position cannot.
int GetFTVariationDesignPosition(FT_Face face, std::span<FontArguments::VariationPosition::Coordinate> coordinates) {
  if (!(face->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS)) {
    return 0;
  }

  FT_MM_Var* variations = nullptr;
  if (FT_Get_MM_Var(face, &variations)) {
    return -1;
  }
  UniqueVoidPtr auto_free_variations(variations);

  if (coordinates.size() < variations->num_axis) {
    return static_cast<int>(variations->num_axis);
  }

  Vector<FT_Fixed, 4> coords(variations->num_axis);
  if (FT_Get_Var_Design_Coordinates(face, variations->num_axis, coords.data())) {
    return -1;
  }
  for (FT_UInt i = 0; i < variations->num_axis; ++i) {
    coordinates[i].axis = static_cast<std::uint32_t>(variations->axis[i].tag);
    coordinates[i].value = FixedToFloat(coords[i]);
  }

  return static_cast<int>(variations->num_axis);
}

// SkFontScanner_FreeType. Only the parts MakeFromStream and onMakeClone use
// are ported.
class FontScannerFreeType {
public:
  FontScannerFreeType()
      : library_(nullptr) {
    if (FT_New_Library(&ft_memory, &library_)) {
      return;
    }
    FT_Add_Default_Modules(library_);
    FT_Set_Default_Properties(library_);
  }
  ~FontScannerFreeType() {
    if (library_) {
      FT_Done_Library(library_);
    }
  }
  FontScannerFreeType(const FontScannerFreeType&) = delete;
  FontScannerFreeType& operator=(const FontScannerFreeType&) = delete;

  bool ScanInstance(StreamAsset* stream,
                    int face_index,
                    int instance_index,
                    String* name,
                    FontStyle* style,
                    bool* is_fixed_pitch,
                    AxisDefinitions* axes,
                    VariationPositionStorage* position) const;

  static void ComputeAxisValues(
      const AxisDefinitions& axis_definitions,
      const FontArguments::VariationPosition current_position,
      const FontArguments::VariationPosition requested_position,
      std::int32_t* axis_values,
      const String& name,
      FontStyle* style);

private:
  FT_Face OpenFace(StreamAsset* stream, int ttc_index, FT_Stream ft_stream) const;
  FT_Library library_;
  mutable Mutex library_mutex_;
};

FT_Face FontScannerFreeType::OpenFace(StreamAsset* stream, int ttc_index, FT_Stream ft_stream) const {
  if (library_ == nullptr || stream == nullptr) {
    return nullptr;
  }

  FT_Open_Args args;
  std::memset(&args, 0, sizeof(args));

  const void* memory_base = stream->GetMemoryBase();

  if (memory_base) {
    args.flags = FT_OPEN_MEMORY;
    args.memory_base = static_cast<const FT_Byte*>(memory_base);
    args.memory_size = static_cast<FT_Long>(stream->GetLength());
  } else {
    std::memset(ft_stream, 0, sizeof(*ft_stream));
    ft_stream->size = static_cast<unsigned long>(stream->GetLength());
    ft_stream->descriptor.pointer = stream;
    ft_stream->read = sk_ft_stream_io;
    ft_stream->close = sk_ft_stream_close;

    args.flags = FT_OPEN_STREAM;
    args.stream = ft_stream;
  }

  FT_Face face;
  if (FT_Open_Face(library_, &args, ttc_index, &face)) {
    return nullptr;
  }
  return face;
}

// SkStrLCSearch over the commonWeights table: lower-cases the ASCII target and
// runs SkStrSearch.
template <typename Entry, std::size_t N>
int StrLCSearch(const Entry (&base)[N], const char target[]) {
  const std::size_t target_len = std::strlen(target);
  Vector<char> lc(static_cast<wtf_size_t>(target_len + 1));
  for (std::size_t i = 0; i < target_len; ++i) {
    char c = target[i];
    lc[static_cast<wtf_size_t>(i)] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
  }
  lc[static_cast<wtf_size_t>(target_len)] = 0;

  const auto compare = [&lc, target_len](const char* elem) -> int {
    int cmp = std::strncmp(elem, lc.data(), target_len);
    if (cmp == 0) {
      cmp = elem[target_len];
    }
    return cmp;
  };

  int lo = 0;
  int hi = static_cast<int>(N) - 1;
  while (lo < hi) {
    int mid = (hi + lo) >> 1;
    if (compare(base[mid].name) < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  int cmp = compare(base[hi].name);
  if (cmp < 0) {
    hi += 1;
    hi = ~hi;
  } else if (cmp > 0) {
    hi = ~hi;
  }
  return hi;
}

bool FontScannerFreeType::ScanInstance(StreamAsset* stream,
                                       int face_index,
                                       int instance_index,
                                       String* name,
                                       FontStyle* style,
                                       bool* is_fixed_pitch,
                                       AxisDefinitions* axes,
                                       VariationPositionStorage* position) const {
  AutoMutexExclusive library_lock(library_mutex_);

  FT_StreamRec stream_rec;
  UniqueFTFace face(OpenFace(stream, (instance_index << 16) + face_index, &stream_rec));
  if (!face) {
    return false;
  }

  int weight = FontStyle::kNormal_Weight;
  int width = FontStyle::kNormal_Width;
  FontStyle::Slant slant = FontStyle::kUpright_Slant;
  if (face->style_flags & FT_STYLE_FLAG_BOLD) {
    weight = FontStyle::kBold_Weight;
  }
  if (face->style_flags & FT_STYLE_FLAG_ITALIC) {
    slant = FontStyle::kItalic_Slant;
  }

  bool has_axes = face->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS;
  TT_OS2* os2 = static_cast<TT_OS2*>(FT_Get_Sfnt_Table(face.get(), FT_SFNT_OS2));
  bool has_os2 = os2 && os2->version != 0xffff;

  PS_FontInfoRec ps_font_info;

  if (has_os2) {
    weight = os2->usWeightClass;
    width = os2->usWidthClass;

    // OS/2::fsSelection bit 9 indicates oblique.
    if (os2->fsSelection & (1u << 9)) {
      slant = FontStyle::kOblique_Slant;
    }
  }

  // Let variable axes override properties from the OS/2 table.
  if (has_axes) {
    AxisDefinitions axis_definitions;
    if (GetAxes(face.get(), &axis_definitions)) {
      std::size_t num_axes = axis_definitions.size();
      constexpr std::uint32_t kWghtTag = MakeTag('w', 'g', 'h', 't');
      constexpr std::uint32_t kWdthTag = MakeTag('w', 'd', 't', 'h');
      constexpr std::uint32_t kSlntTag = MakeTag('s', 'l', 'n', 't');
      std::optional<std::size_t> wght_index;
      std::optional<std::size_t> wdth_index;
      std::optional<std::size_t> slnt_index;
      for (std::size_t i = 0; i < num_axes; ++i) {
        const FontParameters::Variation::Axis& axis = axis_definitions[static_cast<wtf_size_t>(i)];
        if (axis.tag == kWghtTag) {
          // Rough validity check, sufficient spread and ranges within 0-1000.
          float wght_range = axis.max - axis.min;
          if (wght_range > 5 && wght_range <= 1000 && axis.max <= 1000) {
            wght_index = i;
          }
        }
        if (axis.tag == kWdthTag) {
          // Rough validity check, sufficient spread and are ranges within
          // 0-500.
          float wdth_range = axis.max - axis.min;
          if (wdth_range > 0 && wdth_range <= 500 && axis.max <= 500) {
            wdth_index = i;
          }
        }
        if (axis.tag == kSlntTag) {
          slnt_index = i;
        }
      }
      Vector<FT_Fixed, 4> coords(static_cast<wtf_size_t>(num_axes));
      if ((wght_index || wdth_index || slnt_index) &&
          !FT_Get_Var_Design_Coordinates(face.get(), static_cast<FT_UInt>(num_axes), coords.data())) {
        if (wght_index) {
          weight = FixedRoundToInt(static_cast<std::int32_t>(coords[static_cast<wtf_size_t>(*wght_index)]));
        }
        if (wdth_index) {
          float wdth_value = FixedToFloat(coords[static_cast<wtf_size_t>(*wdth_index)]);
          width = FontStyleWidthForWidthAxisValue(wdth_value);
        }
        if (slnt_index) {
          // https://docs.microsoft.com/en-us/typography/opentype/spec/dvaraxistag_slnt
          // "Scale interpretation: Values can be interpreted as the angle,
          // in counter-clockwise degrees, of oblique slant from whatever
          // the designer considers to be upright for that font design."
          if (FixedToFloat(coords[static_cast<wtf_size_t>(*slnt_index)]) < 0) {
            slant = FontStyle::kOblique_Slant;
          }
        }
      }

      if (position) {
        position->resize(static_cast<wtf_size_t>(num_axes));
        if (GetFTVariationDesignPosition(face.get(), std::span(position->data(), num_axes)) != static_cast<int>(num_axes)) {
          return false;
        }
      }
    }
  }

  if (!has_os2 && !has_axes && 0 == FT_Get_PS_Font_Info(face.get(), &ps_font_info) && ps_font_info.weight) {
    static const struct {
      char const* const name;
      int const weight;
    } kCommonWeights[] = {
        // There are probably more common names, but these are known to exist.
        {"all", FontStyle::kNormal_Weight}, // Multiple Masters usually default to normal.
        {"black", FontStyle::kBlack_Weight},
        {"bold", FontStyle::kBold_Weight},
        {"book", (FontStyle::kNormal_Weight + FontStyle::kLight_Weight) / 2},
        {"demi", FontStyle::kSemiBold_Weight},
        {"demibold", FontStyle::kSemiBold_Weight},
        {"extra", FontStyle::kExtraBold_Weight},
        {"extrabold", FontStyle::kExtraBold_Weight},
        {"extralight", FontStyle::kExtraLight_Weight},
        {"hairline", FontStyle::kThin_Weight},
        {"heavy", FontStyle::kBlack_Weight},
        {"light", FontStyle::kLight_Weight},
        {"medium", FontStyle::kMedium_Weight},
        {"normal", FontStyle::kNormal_Weight},
        {"plain", FontStyle::kNormal_Weight},
        {"regular", FontStyle::kNormal_Weight},
        {"roman", FontStyle::kNormal_Weight},
        {"semibold", FontStyle::kSemiBold_Weight},
        {"standard", FontStyle::kNormal_Weight},
        {"thin", FontStyle::kThin_Weight},
        {"ultra", FontStyle::kExtraBold_Weight},
        {"ultrablack", FontStyle::kExtraBlack_Weight},
        {"ultrabold", FontStyle::kExtraBold_Weight},
        {"ultraheavy", FontStyle::kExtraBlack_Weight},
        {"ultralight", FontStyle::kExtraLight_Weight},
    };
    int const index = StrLCSearch(kCommonWeights, ps_font_info.weight);
    if (index >= 0) {
      weight = kCommonWeights[index].weight;
    }
  }

  if (name != nullptr) {
    *name = String::FromUTF8(face->family_name ? face->family_name : "");
  }
  if (style != nullptr) {
    *style = FontStyle(weight, width, slant);
  }
  if (is_fixed_pitch != nullptr) {
    *is_fixed_pitch = FT_IS_FIXED_WIDTH(face);
  }

  if (axes != nullptr && !GetAxes(face.get(), axes)) {
    return false;
  }

  return true;
}

void FontScannerFreeType::ComputeAxisValues(
    const AxisDefinitions& axis_definitions,
    const FontArguments::VariationPosition current,
    const FontArguments::VariationPosition position,
    std::int32_t* axis_values,
    const String&,
    FontStyle* style) {
  constexpr std::uint32_t kWghtTag = MakeTag('w', 'g', 'h', 't');
  constexpr std::uint32_t kWdthTag = MakeTag('w', 'd', 't', 'h');
  constexpr std::uint32_t kSlntTag = MakeTag('s', 'l', 'n', 't');
  int weight = FontStyle::kNormal_Weight;
  int width = FontStyle::kNormal_Width;
  FontStyle::Slant slant = FontStyle::kUpright_Slant;
  if (style) {
    weight = style->GetWeight();
    width = style->GetWidth();
    slant = style->GetSlant();
  }

  for (wtf_size_t i = 0; i < axis_definitions.size(); ++i) {
    const FontParameters::Variation::Axis& axis_definition = axis_definitions[i];
    const float axis_min = axis_definition.min;
    const float axis_max = axis_definition.max;

    // Start with the default value.
    axis_values[i] = FloatToFixed(axis_definition.def);

    // Then the current value.
    for (int j = current.coordinate_count; j-- > 0;) {
      const auto& coordinate = current.coordinates[j];
      if (axis_definition.tag == coordinate.axis) {
        const float axis_value = Pin(coordinate.value, axis_min, axis_max);
        axis_values[i] = FloatToFixed(axis_value);
        break;
      }
    }

    // Then the requested value.
    // The position may be over specified. If there are multiple values for a
    // given axis, use the last one since that's what css-fonts-4 requires.
    for (int j = position.coordinate_count; j-- > 0;) {
      const auto& coordinate = position.coordinates[j];
      if (axis_definition.tag == coordinate.axis) {
        const float axis_value = Pin(coordinate.value, axis_min, axis_max);
        axis_values[i] = FloatToFixed(axis_value);
        break;
      }
    }

    if (style) {
      if (axis_definition.tag == kWghtTag) {
        // Rough validity check, is there sufficient spread and are ranges
        // within 0-1000.
        float wght_range = axis_max - axis_min;
        if (wght_range > 5 && wght_range <= 1000 && axis_max <= 1000) {
          weight = FixedRoundToInt(axis_values[i]);
        }
      }
      if (axis_definition.tag == kWdthTag) {
        // Rough validity check, is there a spread and are ranges within 0-500.
        float wdth_range = axis_max - axis_min;
        if (wdth_range > 0 && wdth_range <= 500 && axis_max <= 500) {
          float wdth_value = FixedToFloat(axis_values[i]);
          width = FontStyleWidthForWidthAxisValue(wdth_value);
        }
      }
      if (axis_definition.tag == kSlntTag && slant != FontStyle::kItalic_Slant) {
        // https://docs.microsoft.com/en-us/typography/opentype/spec/dvaraxistag_slnt
        // "Scale interpretation: Values can be interpreted as the angle,
        // in counter-clockwise degrees, of oblique slant from whatever
        // the designer considers to be upright for that font design."
        if (axis_values[i] == 0) {
          slant = FontStyle::kUpright_Slant;
        } else {
          slant = FontStyle::kOblique_Slant;
        }
      }
    }
    // TODO: warn on defaulted axis?
  }

  if (style) {
    *style = FontStyle(weight, width, slant);
  }
}

} // namespace

Mutex& FreeTypeMutex() {
  static Mutex& mutex = *(new Mutex);
  return mutex;
}

// SkTypeface_FreeType::FaceRec.
class TypefaceFreeType::FaceRec {
public:
  UniqueFTFace face;
  FT_StreamRec ft_stream;
  std::unique_ptr<StreamAsset> stream;
  FT_UShort ft_palette_entry_count = 0;
  // SkColor entries.
  std::unique_ptr<std::uint32_t[]> palette;

  // Will return null on failure. Caller must lock FreeTypeMutex() before
  // calling this function.
  static std::unique_ptr<FaceRec> Make(const TypefaceFreeType* typeface);
  ~FaceRec();

private:
  explicit FaceRec(std::unique_ptr<StreamAsset> stream);
  void SetupAxes(const FontStreamData& data);
  void SetupPalette(const FontStreamData& data);

  // Private to RefFreeTypeLibrary and UnrefFreeTypeLibrary.
  static int ft_count_;

  // Caller must lock FreeTypeMutex() before calling this function.
  static FT_Library RefFreeTypeLibrary() {
    if (0 == ft_count_) {
      ft_library = new FreeTypeLibrary;
    }
    ++ft_count_;
    return ft_library->Library();
  }

  // Caller must lock FreeTypeMutex() before calling this function.
  static void UnrefFreeTypeLibrary() {
    --ft_count_;
    if (0 == ft_count_) {
      delete ft_library;
    }
  }
};

int TypefaceFreeType::FaceRec::ft_count_;

TypefaceFreeType::FaceRec::FaceRec(std::unique_ptr<StreamAsset> file_stream)
    : stream(std::move(file_stream)) {
  std::memset(&ft_stream, 0, sizeof(ft_stream));
  ft_stream.size = static_cast<unsigned long>(stream->GetLength());
  ft_stream.descriptor.pointer = stream.get();
  ft_stream.read = sk_ft_stream_io;
  ft_stream.close = sk_ft_stream_close;

  RefFreeTypeLibrary();
}

TypefaceFreeType::FaceRec::~FaceRec() {
  face.reset(); // Must release face before the library, the library frees existing faces.
  UnrefFreeTypeLibrary();
}

void TypefaceFreeType::FaceRec::SetupAxes(const FontStreamData& data) {
  if (!(face->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS)) {
    return;
  }

  // The SkDEBUGCODE axis count check is not ported.

  Vector<FT_Fixed, 4> coords(static_cast<wtf_size_t>(data.GetAxisCount()));
  for (int i = 0; i < data.GetAxisCount(); ++i) {
    coords[static_cast<wtf_size_t>(i)] = data.GetAxis()[i];
  }
  if (FT_Set_Var_Design_Coordinates(face.get(), static_cast<FT_UInt>(data.GetAxisCount()), coords.data())) {
    return;
  }
}

void TypefaceFreeType::FaceRec::SetupPalette(const FontStreamData& data) {
#ifdef FT_COLOR_H
  FT_Palette_Data palette_data;
  if (FT_Palette_Data_Get(face.get(), &palette_data)) {
    return;
  }

  // Treat out of range values as 0. Still apply overrides.
  // https://www.w3.org/TR/css-fonts-4/#base-palette-desc
  FT_UShort base_palette_index = 0;
  if (0 <= data.GetPaletteIndex() && data.GetPaletteIndex() <= 0xFFFF &&
      static_cast<FT_UShort>(data.GetPaletteIndex()) < palette_data.num_palettes) {
    base_palette_index = static_cast<FT_UShort>(data.GetPaletteIndex());
  }

  FT_Color* ft_palette = nullptr;
  if (FT_Palette_Select(face.get(), base_palette_index, &ft_palette)) {
    return;
  }
  if (!ft_palette) {
    return;
  }
  ft_palette_entry_count = palette_data.num_palette_entries;

  for (int i = 0; i < data.GetPaletteOverrideCount(); ++i) {
    const FontArguments::Palette::Override& palette_override = data.GetPaletteOverrides()[i];
    if (palette_override.index < ft_palette_entry_count) {
      const std::uint32_t& sk_color = palette_override.color;
      FT_Color& ft_color = ft_palette[palette_override.index];
      ft_color.blue = static_cast<FT_Byte>(sk_color & 0xFF);
      ft_color.green = static_cast<FT_Byte>((sk_color >> 8) & 0xFF);
      ft_color.red = static_cast<FT_Byte>((sk_color >> 16) & 0xFF);
      ft_color.alpha = static_cast<FT_Byte>((sk_color >> 24) & 0xFF);
    }
  }

  palette.reset(new std::uint32_t[ft_palette_entry_count]);
  for (int i = 0; i < ft_palette_entry_count; ++i) {
    // SkColorSetARGB.
    palette[i] = (static_cast<std::uint32_t>(ft_palette[i].alpha) << 24) | (static_cast<std::uint32_t>(ft_palette[i].red) << 16) | (static_cast<std::uint32_t>(ft_palette[i].green) << 8) | static_cast<std::uint32_t>(ft_palette[i].blue);
  }
#endif
}

std::unique_ptr<TypefaceFreeType::FaceRec> TypefaceFreeType::FaceRec::Make(const TypefaceFreeType* typeface) {
  std::unique_ptr<FontStreamData> data = typeface->MakeFontData();
  if (nullptr == data || !data->HasStream()) {
    return nullptr;
  }

  std::unique_ptr<FaceRec> rec(new FaceRec(data->DetachStream()));

  FT_Open_Args args;
  std::memset(&args, 0, sizeof(args));
  const void* memory_base = rec->stream->GetMemoryBase();
  if (memory_base) {
    args.flags = FT_OPEN_MEMORY;
    args.memory_base = static_cast<const FT_Byte*>(memory_base);
    args.memory_size = static_cast<FT_Long>(rec->stream->GetLength());
  } else {
    args.flags = FT_OPEN_STREAM;
    args.stream = &rec->ft_stream;
  }

  {
    FT_Face raw_face;
    FT_Error err = FT_Open_Face(ft_library->Library(), &args, data->GetIndex(), &raw_face);
    if (err) {
      return nullptr;
    }
    rec->face.reset(raw_face);
  }

  rec->SetupAxes(*data);
  rec->SetupPalette(*data);

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

namespace {

class AutoFTAccess {
public:
  explicit AutoFTAccess(const TypefaceFreeType* tf)
      : face_rec_(nullptr) {
    FreeTypeMutex().Acquire();
    face_rec_ = tf->GetFaceRec();
  }

  ~AutoFTAccess() {
    FreeTypeMutex().Release();
  }

  AutoFTAccess(const AutoFTAccess&) = delete;
  AutoFTAccess& operator=(const AutoFTAccess&) = delete;

  FT_Face Face() {
    return face_rec_ ? face_rec_->face.get() : nullptr;
  }

private:
  TypefaceFreeType::FaceRec* face_rec_;
};

bool BothZero(float a, float b) {
  return 0 == a && 0 == b;
}

// returns false if there is any non-90-rotation or skew
bool IsAxisAligned(const ScalerContextRec& rec) {
  return 0 == rec.pre_skew_x && (BothZero(rec.post2x2[0][1], rec.post2x2[1][0]) || BothZero(rec.post2x2[0][0], rec.post2x2[1][1]));
}

// Returns the bitmap strike equal to or just larger than the requested size.
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

class ScalerContextFreeType final : public ScalerContext {
public:
  ScalerContextFreeType(const TypefaceFreeType& real_typeface,
                        const ScalerContextRec& rec,
                        std::shared_ptr<Typeface> proxy_typeface);
  ~ScalerContextFreeType() override;

  bool Success() const {
    return ft_size_ != nullptr && face_ != nullptr;
  }

protected:
  GlyphMetrics GenerateMetrics(const PlatformGlyph& glyph, Arena* arena) override;
  void GenerateImage(const PlatformGlyph& glyph, void* image_buffer) override;
  std::optional<GeneratedPath> GeneratePath(const PlatformGlyph& glyph) override;
  std::shared_ptr<Drawable> GenerateDrawable(const PlatformGlyph& glyph) override;
  void GenerateFontMetrics(PlatformFontMetrics* metrics) override;

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
  bool GetCBoxForLetter(char letter, FT_BBox* bbox);
  static void UpdateGlyphBoundsIfSubpixel(const PlatformGlyph& glyph, ScalarRect* bounds, bool subpixel);
  void UpdateGlyphBoundsIfLcd(GlyphMetrics* mx);
  // Caller must lock FreeTypeMutex() before calling this function.
  // update FreeType2 glyph slot with glyph emboldened
  bool EmboldenIfNeeded(FT_Face face, FT_GlyphSlot glyph, std::uint16_t gid);
  bool ShouldSubpixelBitmap(const PlatformGlyph& glyph, const ScalarMatrix& matrix);
  std::span<ColorARGB> Palette() const;

  TypefaceFreeType::FaceRec* face_rec_; // Borrowed face from the typeface's FaceRec.
  FT_Face face_;                        // Borrowed face from face_rec_.
  FT_Size ft_size_;                     // The size to apply to the face_.
  FT_Int strike_index_;                 // The bitmap strike for the face_ (or -1 if none).

  ScalerContextFTUtils utils_;

  // The rest of the matrix after FreeType handles the size. With outline font
  // rasterization this is handled by FreeType with FT_Set_Transform. With
  // bitmap only fonts this matrix must be applied to scale the bitmap.
  ScalarMatrix matrix22_scalar_;
  FT_Matrix matrix22_;
  ScalarPoint scale_;
  FT_Int32 load_glyph_flags_;
  bool do_linear_metrics_;
  bool lcd_is_vert_;
};

ScalerContextFreeType::ScalerContextFreeType(const TypefaceFreeType& real_typeface,
                                             const ScalerContextRec& rec,
                                             std::shared_ptr<Typeface> proxy_typeface)
    : ScalerContext(std::move(proxy_typeface), rec),
      face_rec_(nullptr),
      face_(nullptr),
      ft_size_(nullptr),
      strike_index_(-1),
      matrix22_(),
      load_glyph_flags_(0),
      do_linear_metrics_(false),
      lcd_is_vert_(false) {
  AutoMutexExclusive ac(FreeTypeMutex());
  face_rec_ = real_typeface.GetFaceRec(); // The proxy typeface owns the real typeface.

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
      float x_ppem = upem * FTFixedToScalar(ftmetrics.x_scale) / 64.0f;
      float y_ppem = upem * FTFixedToScalar(ftmetrics.y_scale) / 64.0f;
      matrix22_scalar_.PreScale(scale_.x / x_ppem, scale_.y / y_ppem);
    }

    // FT_LOAD_COLOR with scalable fonts means allow SVG.
    // It also implies attempt to render COLR if available, but this is not
    // used.
#if defined(FT_CONFIG_OPTION_SVG)
    if (GetOpenTypeSVGDecoderFactory()) {
      load_glyph_flags_ |= FT_LOAD_COLOR;
    }
#endif
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
  utils_.Init(rec_.foreground_color, static_cast<ScalerContext::Flags>(rec_.flags));
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

bool ScalerContextFreeType::GetCBoxForLetter(char letter, FT_BBox* bbox) {
  const FT_UInt glyph_id = FT_Get_Char_Index(face_, static_cast<FT_ULong>(letter));
  if (!glyph_id) {
    return false;
  }
  if (FT_Load_Glyph(face_, glyph_id, load_glyph_flags_)) {
    return false;
  }
  if (face_->glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
    return false;
  }
  EmboldenIfNeeded(face_, face_->glyph, static_cast<std::uint16_t>(glyph_id));
  FT_Outline_Get_CBox(&face_->glyph->outline, bbox);
  return true;
}

void ScalerContextFreeType::UpdateGlyphBoundsIfSubpixel(const PlatformGlyph& glyph, ScalarRect* bounds, bool subpixel) {
  if (subpixel && !bounds->IsEmpty()) {
    bounds->Offset(FixedToFloat(glyph.GetSubXFixed()), FixedToFloat(glyph.GetSubYFixed()));
  }
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

bool ScalerContextFreeType::ShouldSubpixelBitmap(const PlatformGlyph& glyph, const ScalarMatrix& matrix) {
  // If subpixel rendering of a bitmap *can* be done.
  bool mechanism = face_->glyph->format == FT_GLYPH_FORMAT_BITMAP &&
                   IsSubpixel() &&
                   (glyph.GetSubXFixed() || glyph.GetSubYFixed());

  // If subpixel rendering of a bitmap *should* be done.
  // 1. If the face is not scalable then always allow subpixel rendering.
  //    Otherwise, if the font has an 8ppem strike 7 will subpixel render but 8
  //    won't.
  // 2. If the matrix is already not identity the bitmap will already be
  //    resampled, so resampling slightly differently shouldn't make much
  //    difference.
  bool policy = !FT_IS_SCALABLE(face_) || !matrix.IsIdentity();

  return mechanism && policy;
}

ScalerContext::GlyphMetrics ScalerContextFreeType::GenerateMetrics(const PlatformGlyph& glyph, Arena*) {
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
      UpdateGlyphBoundsIfSubpixel(glyph, &bounds, IsSubpixel());
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
      UpdateGlyphBoundsIfSubpixel(glyph, &mx.bounds, IsSubpixel());
      UpdateGlyphBoundsIfLcd(&mx);

    } else if (face_->glyph->format == FT_GLYPH_FORMAT_BITMAP) {
      mx.never_request_path = true;

      if (face_->glyph->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA) {
        mx.mask_format = MaskFormat::kARGB32;
      }

      mx.bounds = ScalarRect::MakeXYWH(static_cast<float>(face_->glyph->bitmap_left), -static_cast<float>(face_->glyph->bitmap_top), static_cast<float>(face_->glyph->bitmap.width), static_cast<float>(face_->glyph->bitmap.rows));
      matrix22_scalar_.MapRect(&mx.bounds);
      UpdateGlyphBoundsIfSubpixel(glyph, &mx.bounds, ShouldSubpixelBitmap(glyph, matrix22_scalar_));

#if defined(FT_CONFIG_OPTION_SVG)
    } else if (face_->glyph->format == FT_GLYPH_FORMAT_SVG) {
      mx.extra_bits = ScalerContextBits::kSvg;
      mx.mask_format = MaskFormat::kARGB32;
      mx.never_request_path = true;

      PictureRecorder recorder;
      constexpr float kInfinity = std::numeric_limits<float>::infinity();
      ScalarRect infinite_rect = ScalarRect::MakeLTRB(-kInfinity, -kInfinity, kInfinity, kInfinity);
      // The bounds of the recorded content become the cull rect, as an
      // SkRTree bounding box hierarchy makes them.
      Canvas* recording_canvas = recorder.BeginRecording(infinite_rect, true);
      if (!utils_.DrawSVGGlyph(face_, glyph, static_cast<std::uint32_t>(load_glyph_flags_), Palette(), recording_canvas)) {
        return mx;
      }
      std::shared_ptr<const Picture> pic = recorder.FinishRecordingAsPicture();
      mx.bounds = pic->CullRect();
      // DrawSVGGlyph already applied the subpixel positioning.
#endif // FT_CONFIG_OPTION_SVG

    } else {
      return mx;
    }
  }

  // SkScalerContext::isVertical() is always false.
  if (do_linear_metrics_) {
    const float advance_scalar = FTFixedToScalar(face_->glyph->linearHoriAdvance);
    mx.advance.x = matrix22_scalar_.GetScaleX() * advance_scalar;
    mx.advance.y = matrix22_scalar_.GetSkewY() * advance_scalar;
  } else {
    mx.advance.x = FDot6ToFloat(face_->glyph->advance.x);
    mx.advance.y = -FDot6ToFloat(face_->glyph->advance.y);
  }

  return mx;
}

void ScalerContextFreeType::GenerateImage(const PlatformGlyph& glyph, void* image_buffer) {
  AutoMutexExclusive ac(FreeTypeMutex());

  if (SetupSize()) {
    std::memset(image_buffer, 0, glyph.ImageSize());
    return;
  }

  if (glyph.ExtraBits() == ScalerContextBits::kColrV0 ||
      glyph.ExtraBits() == ScalerContextBits::kColrV1 ||
      glyph.ExtraBits() == ScalerContextBits::kSvg) {
    // TODO: mark this as sRGB when the blits will be sRGB.
    Pixmap dst_pixmap(ColorType::kN32, glyph.Width(), glyph.Height(), image_buffer, glyph.RowBytes());

    RasterCanvas canvas(dst_pixmap);
    canvas.Clear(kColorTransparent);
    canvas.Translate(static_cast<float>(-glyph.Left()), static_cast<float>(-glyph.Top()));

    const std::uint32_t load_flags = static_cast<std::uint32_t>(load_glyph_flags_);
    if (glyph.ExtraBits() == ScalerContextBits::kColrV0) {
      utils_.DrawCOLRv0Glyph(face_, glyph, load_flags, Palette(), &canvas);
    } else if (glyph.ExtraBits() == ScalerContextBits::kColrV1) {
      utils_.DrawCOLRv1Glyph(face_, glyph, load_flags, Palette(), &canvas);
    } else if (glyph.ExtraBits() == ScalerContextBits::kSvg) {
#if defined(FT_CONFIG_OPTION_SVG)
      if (FT_Load_Glyph(face_, glyph.GetGlyphID(), load_glyph_flags_)) {
        return;
      }
      utils_.DrawSVGGlyph(face_, glyph, load_flags, Palette(), &canvas);
#endif
    }
    return;
  }

  if (FT_Load_Glyph(face_, glyph.GetGlyphID(), load_glyph_flags_)) {
    std::memset(image_buffer, 0, glyph.ImageSize());
    return;
  }
  EmboldenIfNeeded(face_, face_->glyph, glyph.GetGlyphID());

  const ScalarMatrix* bitmap_matrix = &matrix22_scalar_;
  ScalarMatrix subpixel_bitmap_matrix;
  if (ShouldSubpixelBitmap(glyph, *bitmap_matrix)) {
    subpixel_bitmap_matrix = matrix22_scalar_;
    subpixel_bitmap_matrix.PostConcat(ScalarMatrix::Translate(FixedToFloat(glyph.GetSubXFixed()),
                                                              FixedToFloat(glyph.GetSubYFixed())));
    bitmap_matrix = &subpixel_bitmap_matrix;
  }

  utils_.GenerateGlyphImage(face_, glyph, image_buffer, *bitmap_matrix, pre_blend_);
}

std::shared_ptr<Drawable> ScalerContextFreeType::GenerateDrawable(const PlatformGlyph& glyph) {
  // Because FreeType's FT_Face is stateful (not thread safe) and the current
  // design of this Typeface and ScalerContext does not work around this, it
  // is necessary lock at least the FT_Face when using it (this implementation
  // currently locks the whole FT_Library). It should be possible to draw the
  // drawable straight out of the FT_Face. However, this would mean locking
  // each time any such drawable is drawn. To avoid locking, this
  // implementation creates drawables backed as pictures so that they can be
  // played back later without locking.
  AutoMutexExclusive ac(FreeTypeMutex());

  if (SetupSize()) {
    return nullptr;
  }

  if (glyph.ExtraBits() == ScalerContextBits::kColrV0 ||
      glyph.ExtraBits() == ScalerContextBits::kColrV1 ||
      glyph.ExtraBits() == ScalerContextBits::kSvg) {
    const std::uint32_t load_flags = static_cast<std::uint32_t>(load_glyph_flags_);
    PictureRecorder recorder;
    const IntRect mask_bounds = glyph.GetMask().bounds;
    Canvas* recording_canvas = recorder.BeginRecording(
        ScalarRect::MakeLTRB(static_cast<float>(mask_bounds.left), static_cast<float>(mask_bounds.top),
                             static_cast<float>(mask_bounds.right), static_cast<float>(mask_bounds.bottom)));
    if (glyph.ExtraBits() == ScalerContextBits::kColrV0) {
      if (!utils_.DrawCOLRv0Glyph(face_, glyph, load_flags, Palette(), recording_canvas)) {
        return nullptr;
      }
    } else if (glyph.ExtraBits() == ScalerContextBits::kColrV1) {
      if (!utils_.DrawCOLRv1Glyph(face_, glyph, load_flags, Palette(), recording_canvas)) {
        return nullptr;
      }
    } else if (glyph.ExtraBits() == ScalerContextBits::kSvg) {
#if defined(FT_CONFIG_OPTION_SVG)
      if (FT_Load_Glyph(face_, glyph.GetGlyphID(), load_glyph_flags_)) {
        return nullptr;
      }
      if (!utils_.DrawSVGGlyph(face_, glyph, load_flags, Palette(), recording_canvas)) {
        return nullptr;
      }
#else
      return nullptr;
#endif
    }
    return recorder.FinishRecordingAsDrawable();
  }
  return nullptr;
}

std::span<ColorARGB> ScalerContextFreeType::Palette() const {
  return std::span<ColorARGB>(face_rec_->palette.get(), face_rec_->ft_palette_entry_count);
}

std::optional<ScalerContext::GeneratedPath> ScalerContextFreeType::GeneratePath(const PlatformGlyph& glyph) {
  AutoMutexExclusive ac(FreeTypeMutex());

  std::uint16_t glyph_id = glyph.GetGlyphID();
  // FT_IS_SCALABLE is documented to mean the face contains outline glyphs.
  if (!FT_IS_SCALABLE(face_) || SetupSize()) {
    return {};
  }

  std::uint32_t flags = static_cast<std::uint32_t>(load_glyph_flags_);
  flags |= FT_LOAD_NO_BITMAP; // ignore embedded bitmaps so we're sure to get the outline
  flags &= ~FT_LOAD_RENDER;   // don't scan convert (we just want the outline)

  FT_Error err = FT_Load_Glyph(face_, glyph_id, static_cast<FT_Int32>(flags));
  if (err != 0 || face_->glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
    return {};
  }

  bool modified = EmboldenIfNeeded(face_, face_->glyph, glyph_id);

  ScalarPath path;
  if (!ScalerContextFTUtils::GenerateGlyphPath(face_, &path)) {
    return {};
  }

  // The path's origin from FreeType is always the horizontal layout origin;
  // isVertical() is always false, so no vertical offset is applied.
  return GeneratedPath{std::move(path), modified};
}

void ScalerContextFreeType::GenerateFontMetrics(PlatformFontMetrics* metrics) {
  if (nullptr == metrics) {
    return;
  }

  AutoMutexExclusive ac(FreeTypeMutex());

  if (SetupSize()) {
    *metrics = PlatformFontMetrics();
    return;
  }

  FT_Face face = face_;
  metrics->flags = 0;

  float upem = static_cast<float>(TypefaceFreeType::GetUnitsPerEm(face));

  // use the os/2 table as a source of reasonable defaults.
  float x_height = 0.0f;
  float avg_char_width = 0.0f;
  float cap_height = 0.0f;
  float strikeout_thickness = 0.0f, strikeout_position = 0.0f;
  TT_OS2* os2 = static_cast<TT_OS2*>(FT_Get_Sfnt_Table(face, FT_SFNT_OS2));
  if (os2) {
    x_height = static_cast<float>(os2->sxHeight) / upem * scale_.y;
    avg_char_width = static_cast<float>(os2->xAvgCharWidth) / upem;
    strikeout_thickness = static_cast<float>(os2->yStrikeoutSize) / upem;
    strikeout_position = -static_cast<float>(os2->yStrikeoutPosition) / upem;
    metrics->flags |= PlatformFontMetrics::kStrikeoutThicknessIsValid_Flag;
    metrics->flags |= PlatformFontMetrics::kStrikeoutPositionIsValid_Flag;
    if (os2->version != 0xFFFF && os2->version >= 2) {
      cap_height = static_cast<float>(os2->sCapHeight) / upem * scale_.y;
    }
  }

  // pull from format-specific metrics as needed
  float ascent, descent, leading, xmin, xmax, ymin, ymax;
  float underline_thickness, underline_position;
  if (face->face_flags & FT_FACE_FLAG_SCALABLE) { // scalable outline font
    // FreeType will always use HHEA metrics if they're not zero.
    // It completely ignores the OS/2 fsSelection::UseTypoMetrics bit.
    // It also ignores the VDMX tables, which are also of interest here
    // (and override everything else when they apply).
    static const int kUseTypoMetricsMask = (1 << 7);
    if (os2 && os2->version != 0xFFFF && (os2->fsSelection & kUseTypoMetricsMask)) {
      ascent = -static_cast<float>(os2->sTypoAscender) / upem;
      descent = -static_cast<float>(os2->sTypoDescender) / upem;
      leading = static_cast<float>(os2->sTypoLineGap) / upem;
    } else {
      ascent = -static_cast<float>(face->ascender) / upem;
      descent = -static_cast<float>(face->descender) / upem;
      leading = static_cast<float>(face->height + (face->descender - face->ascender)) / upem;
    }
    xmin = static_cast<float>(face->bbox.xMin) / upem;
    xmax = static_cast<float>(face->bbox.xMax) / upem;
    ymin = -static_cast<float>(face->bbox.yMin) / upem;
    ymax = -static_cast<float>(face->bbox.yMax) / upem;
    underline_thickness = static_cast<float>(face->underline_thickness) / upem;
    underline_position = -static_cast<float>(face->underline_position + face->underline_thickness / 2) / upem;

    metrics->flags |= PlatformFontMetrics::kUnderlineThicknessIsValid_Flag;
    metrics->flags |= PlatformFontMetrics::kUnderlinePositionIsValid_Flag;

    // we may be able to synthesize x_height and cap_height from outline
    if (!x_height) {
      FT_BBox bbox;
      if (GetCBoxForLetter('x', &bbox)) {
        x_height = static_cast<float>(bbox.yMax) / 64.0f;
      }
    }
    if (!cap_height) {
      FT_BBox bbox;
      if (GetCBoxForLetter('H', &bbox)) {
        cap_height = static_cast<float>(bbox.yMax) / 64.0f;
      }
    }
  } else if (strike_index_ != -1) { // bitmap strike metrics
    float xppem = static_cast<float>(face->size->metrics.x_ppem);
    float yppem = static_cast<float>(face->size->metrics.y_ppem);
    ascent = -static_cast<float>(face->size->metrics.ascender) / (yppem * 64.0f);
    descent = -static_cast<float>(face->size->metrics.descender) / (yppem * 64.0f);
    leading = (static_cast<float>(face->size->metrics.height) / (yppem * 64.0f)) + ascent - descent;

    xmin = 0.0f;
    xmax = static_cast<float>(face->available_sizes[strike_index_].width) / xppem;
    ymin = descent;
    ymax = ascent;
    // The actual bitmaps may be any size and placed at any offset.
    metrics->flags |= PlatformFontMetrics::kBoundsInvalid_Flag;

    underline_thickness = 0;
    underline_position = 0;
    metrics->flags &= ~PlatformFontMetrics::kUnderlineThicknessIsValid_Flag;
    metrics->flags &= ~PlatformFontMetrics::kUnderlinePositionIsValid_Flag;

    TT_Postscript* post = static_cast<TT_Postscript*>(FT_Get_Sfnt_Table(face, FT_SFNT_POST));
    if (post) {
      underline_thickness = static_cast<float>(post->underlineThickness) / upem;
      underline_position = -static_cast<float>(post->underlinePosition) / upem;
      metrics->flags |= PlatformFontMetrics::kUnderlineThicknessIsValid_Flag;
      metrics->flags |= PlatformFontMetrics::kUnderlinePositionIsValid_Flag;
    }
  } else {
    *metrics = PlatformFontMetrics();
    return;
  }

  // synthesize elements that were not provided by the os/2 table or
  // format-specific metrics
  if (!x_height) {
    x_height = -ascent * scale_.y;
  }
  if (!avg_char_width) {
    avg_char_width = xmax - xmin;
  }
  if (!cap_height) {
    cap_height = -ascent * scale_.y;
  }

  // disallow negative linespacing
  if (leading < 0.0f) {
    leading = 0.0f;
  }

  metrics->top = ymax * scale_.y;
  metrics->ascent = ascent * scale_.y;
  metrics->descent = descent * scale_.y;
  metrics->bottom = ymin * scale_.y;
  metrics->leading = leading * scale_.y;
  metrics->avg_char_width = avg_char_width * scale_.y;
  metrics->x_min = xmin * scale_.y;
  metrics->x_max = xmax * scale_.y;
  metrics->max_char_width = metrics->x_max - metrics->x_min;
  metrics->x_height = x_height;
  metrics->cap_height = cap_height;
  metrics->underline_thickness = underline_thickness * scale_.y;
  metrics->underline_position = underline_position * scale_.y;
  metrics->strikeout_thickness = strikeout_thickness * scale_.y;
  metrics->strikeout_position = strikeout_position * scale_.y;

  if (face->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS
#if defined(FT_CONFIG_OPTION_SVG)
      || face->face_flags & FT_FACE_FLAG_SVG
#endif // FT_CONFIG_OPTION_SVG
  ) {
    // The bounds are only valid for the default variation of variable glyphs.
    // https://docs.microsoft.com/en-us/typography/opentype/spec/head
    // For SVG glyphs this number is often incorrect for its non-`glyf` points.
    // https://github.com/fonttools/fonttools/issues/2566
    metrics->flags |= PlatformFontMetrics::kBoundsInvalid_Flag;
  }
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

bool TypefaceFreeType::OnGetPostScriptName(String* post_script_name) const {
  AutoFTAccess fta(this);
  FT_Face face = fta.Face();
  if (!face) {
    return false;
  }

  const char* ft_post_script_name = FT_Get_Postscript_Name(face);
  if (!ft_post_script_name) {
    return false;
  }
  if (post_script_name) {
    *post_script_name = String::FromUTF8(ft_post_script_name);
  }
  return true;
}

std::unique_ptr<ScalerContext> TypefaceFreeType::OnCreateScalerContext(const ScalerContextRec& rec) const {
  return OnCreateScalerContextAsProxyTypeface(rec, nullptr);
}

std::unique_ptr<ScalerContext> TypefaceFreeType::OnCreateScalerContextAsProxyTypeface(const ScalerContextRec& rec, Typeface* proxy_typeface) const {
  std::unique_ptr<ScalerContextFreeType> scaler_context = std::make_unique<ScalerContextFreeType>(
      *this,
      rec,
      proxy_typeface ? proxy_typeface->shared_from_this() : RefThis());
  if (scaler_context->Success()) {
    return scaler_context;
  }
  return ScalerContext::MakeEmpty(RefThis(), rec);
}

std::unique_ptr<FontStreamData> TypefaceFreeType::CloneFontData(const FontArguments& args, FontStyle* style) const {
  AutoFTAccess fta(this);
  FT_Face face = fta.Face();
  if (!face) {
    return nullptr;
  }

  AxisDefinitions axis_definitions;
  if (!GetAxes(face, &axis_definitions)) {
    return nullptr;
  }
  int axis_count = static_cast<int>(axis_definitions.size());

  VariationPositionStorage current_position(static_cast<wtf_size_t>(axis_count));
  int current_axis_count = GetFTVariationDesignPosition(face, std::span(current_position.data(), current_position.size()));

  String name;
  Vector<std::int32_t, 4> axis_values(static_cast<wtf_size_t>(axis_count));
  FontScannerFreeType::ComputeAxisValues(
      axis_definitions,
      current_axis_count == axis_count
          ? FontArguments::VariationPosition{current_position.data(), current_axis_count}
          : FontArguments::VariationPosition{nullptr, 0},
      args.GetVariationDesignPosition(),
      axis_values.data(), name, style);

  int ttc_index;
  std::unique_ptr<StreamAsset> stream = OpenStream(&ttc_index);

  return std::make_unique<FontStreamData>(std::move(stream),
                                          ttc_index,
                                          args.GetPalette().index,
                                          axis_values.data(),
                                          axis_count,
                                          args.GetPalette().overrides,
                                          args.GetPalette().override_count);
}

void TypefaceFreeType::OnFilterRec(ScalerContextRec* rec) const {
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

  // SK_GAMMA_APPLY_TO_A8 is not defined.
  if (!IsLcd(*rec)) {
    // SRGBTODO: Is this correct? Do we want contrast boost?
    rec->IgnorePreBlend();
  }
}

int TypefaceFreeType::GetUnitsPerEm(FT_Face face) {
  float upem = static_cast<float>(face->units_per_EM);
  // At least some versions of FreeType set face->units_per_EM to 0 for bitmap
  // only fonts.
  if (upem == 0) {
    TT_Header* tt_header = static_cast<TT_Header*>(FT_Get_Sfnt_Table(face, FT_SFNT_HEAD));
    if (tt_header) {
      upem = static_cast<float>(tt_header->Units_Per_EM);
    }
  }
  return static_cast<int>(upem);
}

int TypefaceFreeType::OnGetUPEM() const {
  AutoFTAccess fta(this);
  FT_Face face = fta.Face();
  if (!face) {
    return 0;
  }
  return GetUnitsPerEm(face);
}

TypefaceFreeType::TypefaceFreeType(const FontStyle& style, bool is_fixed_pitch)
    : Typeface(style, is_fixed_pitch) {
}

TypefaceFreeType::~TypefaceFreeType() {
  if (face_rec_) {
    AutoMutexExclusive ac(FreeTypeMutex());
    face_rec_.reset();
  }
}

void TypefaceFreeType::OnCharsToGlyphs(std::span<const std::int32_t> uni, std::span<std::uint16_t> glyphs) const {
  AutoFTAccess fta(this);
  FT_Face face = fta.Face();
  if (!face) {
    std::fill(glyphs.begin(), glyphs.end(), static_cast<std::uint16_t>(0));
    return;
  }

  const std::size_t count = uni.size();
  for (std::size_t i = 0; i < count; ++i) {
    glyphs[i] = static_cast<std::uint16_t>(FT_Get_Char_Index(face, static_cast<FT_ULong>(uni[i])));
  }
}

int TypefaceFreeType::OnCountGlyphs() const {
  AutoFTAccess fta(this);
  FT_Face face = fta.Face();
  return face ? static_cast<int>(face->num_glyphs) : 0;
}

std::unique_ptr<Typeface::LocalizedStrings> TypefaceFreeType::OnCreateFamilyNameIterator() const {
  std::unique_ptr<Typeface::LocalizedStrings> name_iter = LocalizedStringsNameTable::MakeForFamilyNames(*this);
  if (!name_iter) {
    String family_name = GetFamilyName();
    String language("und"); // undetermined
    name_iter = std::make_unique<LocalizedStringsSingleName>(family_name, language);
  }
  return name_iter;
}

bool TypefaceFreeType::OnGlyphMaskNeedsCurrentColor() const {
  glyph_masks_may_need_current_color_once_([this] {
    constexpr std::uint32_t kColrTag = MakeTag('C', 'O', 'L', 'R');
    glyph_masks_may_need_current_color_ = GetTableSize(kColrTag) > 0;
#if defined(FT_CONFIG_OPTION_SVG)
    constexpr std::uint32_t kSvgTag = MakeTag('S', 'V', 'G', ' ');
    glyph_masks_may_need_current_color_ |= GetTableSize(kSvgTag) > 0;
#endif // FT_CONFIG_OPTION_SVG
  });
  return glyph_masks_may_need_current_color_;
}

int TypefaceFreeType::OnGetVariationDesignPosition(std::span<FontArguments::VariationPosition::Coordinate> coordinates) const {
  AutoFTAccess fta(this);
  FT_Face face = fta.Face();
  if (!face) {
    return -1;
  }
  return GetFTVariationDesignPosition(face, coordinates);
}

int TypefaceFreeType::OnGetVariationDesignParameters(std::span<FontParameters::Variation::Axis> parameters) const {
  AutoFTAccess fta(this);
  FT_Face face = fta.Face();
  if (!face) {
    return -1;
  }

  if (!(face->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS)) {
    return 0;
  }

  FT_MM_Var* variations = nullptr;
  if (FT_Get_MM_Var(face, &variations)) {
    return -1;
  }
  UniqueVoidPtr auto_free_variations(variations);

  if (parameters.size() < variations->num_axis) {
    return static_cast<int>(variations->num_axis);
  }

  for (FT_UInt i = 0; i < variations->num_axis; ++i) {
    parameters[i].tag = static_cast<std::uint32_t>(variations->axis[i].tag);
    parameters[i].min = FixedToFloat(variations->axis[i].minimum);
    parameters[i].def = FixedToFloat(variations->axis[i].def);
    parameters[i].max = FixedToFloat(variations->axis[i].maximum);
    FT_UInt flags = 0;
    bool hidden = !FT_Get_Var_Axis_Flags(variations, i, &flags) &&
                  (flags & FT_VAR_AXIS_FLAG_HIDDEN);
    parameters[i].SetHidden(hidden);
  }

  return static_cast<int>(variations->num_axis);
}

int TypefaceFreeType::OnGetTableTags(std::span<std::uint32_t> tags) const {
  AutoFTAccess fta(this);
  FT_Face face = fta.Face();
  if (!face) {
    return 0;
  }

  FT_ULong table_count = 0;
  FT_Error error;

  // When 'tag' is nullptr, returns number of tables in 'length'.
  error = FT_Sfnt_Table_Info(face, 0, nullptr, &table_count);
  if (error) {
    return 0;
  }

  const std::size_t count = std::min<std::size_t>(table_count, tags.size());
  for (std::size_t table_index = 0; table_index < count; ++table_index) {
    FT_ULong table_tag;
    FT_ULong table_length;
    error = FT_Sfnt_Table_Info(face, static_cast<FT_UInt>(table_index), &table_tag, &table_length);
    if (error) {
      return 0;
    }
    tags[table_index] = static_cast<std::uint32_t>(table_tag);
  }
  return static_cast<int>(table_count);
}

std::size_t TypefaceFreeType::OnGetTableData(std::uint32_t tag, std::size_t offset, std::size_t length, void* data) const {
  AutoFTAccess fta(this);
  FT_Face face = fta.Face();
  if (!face) {
    return 0;
  }

  FT_ULong table_length = 0;
  FT_Error error;

  // When 'length' is 0 it is overwritten with the full table length; 'offset'
  // is ignored.
  error = FT_Load_Sfnt_Table(face, tag, 0, nullptr, &table_length);
  if (error) {
    return 0;
  }

  if (offset > table_length) {
    return 0;
  }
  FT_ULong size = std::min(static_cast<FT_ULong>(length), table_length - static_cast<FT_ULong>(offset));
  if (data) {
    error = FT_Load_Sfnt_Table(face, tag, static_cast<FT_Long>(offset), static_cast<FT_Byte*>(data), &size);
    if (error) {
      return 0;
    }
  }

  return size;
}

std::shared_ptr<Data> TypefaceFreeType::OnCopyTableData(std::uint32_t tag) const {
  AutoFTAccess fta(this);
  FT_Face face = fta.Face();
  if (!face) {
    return nullptr;
  }

  FT_ULong table_length = 0;
  FT_Error error;

  // When 'length' is 0 it is overwritten with the full table length; 'offset'
  // is ignored.
  error = FT_Load_Sfnt_Table(face, tag, 0, nullptr, &table_length);
  if (error) {
    return nullptr;
  }

  std::shared_ptr<Data> data = Data::MakeUninitialized(table_length);
  if (data) {
    error = FT_Load_Sfnt_Table(face, tag, 0, static_cast<FT_Byte*>(data->writable_data()), &table_length);
    if (error) {
      data.reset();
    }
  }
  return data;
}

TypefaceFreeType::FaceRec* TypefaceFreeType::GetFaceRec() const {
  ft_face_once_([this] {
    face_rec_ = TypefaceFreeType::FaceRec::Make(this);
  });
  return face_rec_.get();
}

std::unique_ptr<FontStreamData> TypefaceFreeType::MakeFontData() const {
  return OnMakeFontData();
}

TypefaceFreeTypeStream::TypefaceFreeTypeStream(std::unique_ptr<FontStreamData> font_data,
                                               const String family_name,
                                               const FontStyle& style, bool is_fixed_pitch)
    : TypefaceFreeType(style, is_fixed_pitch),
      family_name_(std::move(family_name)),
      data_(std::move(font_data)) {
}

TypefaceFreeTypeStream::~TypefaceFreeTypeStream() = default;

void TypefaceFreeTypeStream::OnGetFamilyName(String* family_name) const {
  *family_name = family_name_;
}

std::unique_ptr<StreamAsset> TypefaceFreeTypeStream::OnOpenStream(int* ttc_index) const {
  *ttc_index = data_->GetIndex();
  return data_->GetStream()->Duplicate();
}

std::unique_ptr<FontStreamData> TypefaceFreeTypeStream::OnMakeFontData() const {
  return std::make_unique<FontStreamData>(*data_);
}

std::shared_ptr<Typeface> TypefaceFreeTypeStream::OnMakeClone(const FontArguments& args) const {
  FontStyle style = GetFontStyle();
  std::unique_ptr<FontStreamData> data = CloneFontData(args, &style);
  if (!data) {
    return nullptr;
  }

  String family_name = GetFamilyName();

  return std::make_shared<TypefaceFreeTypeStream>(
      std::move(data), family_name, style, IsFixedPitch());
}

std::shared_ptr<Typeface> TypefaceFreeType::MakeFromStream(std::unique_ptr<StreamAsset> stream,
                                                           const FontArguments& args) {
  static FontScannerFreeType& scanner = *new FontScannerFreeType;
  bool is_fixed_pitch;
  FontStyle style;
  String name;
  AxisDefinitions axis_definitions;
  VariationPositionStorage current;
  if (!scanner.ScanInstance(stream.get(), args.GetCollectionIndex(), 0,
                            &name, &style, &is_fixed_pitch, &axis_definitions, &current)) {
    return nullptr;
  }

  Vector<std::int32_t, 4> axis_values(axis_definitions.size());
  FontScannerFreeType::ComputeAxisValues(
      axis_definitions,
      FontArguments::VariationPosition{current.data(), static_cast<int>(current.size())},
      args.GetVariationDesignPosition(),
      axis_values.data(), name, &style);

  auto data = std::make_unique<FontStreamData>(
      std::move(stream), args.GetCollectionIndex(), args.GetPalette().index,
      axis_values.data(), static_cast<int>(axis_definitions.size()),
      args.GetPalette().overrides, args.GetPalette().override_count);
  return std::make_shared<TypefaceFreeTypeStream>(std::move(data), name, style, is_fixed_pitch);
}

} // namespace bkfont
