// Ported from: skia/src/ports/SkFontHost_FreeType.cpp

#pragma once

#include <memory>

#include <ft2build.h>
#include <freetype/freetype.h>

#include "base/mutex.h"
#include "font_face.h"

namespace bkfont {

class ScalerContext;
struct ScalerContextRec;

// f_t_mutex. Caller must lock it before calling into FreeType.
Mutex& FreeTypeMutex();

struct FreeTypeFaceDeleter {
  void operator()(FT_Face face) const {
    FT_Done_Face(face);
  }
};

// SkTypeface_FreeType::FaceRec. The palette members are not ported; they only
// feed COLR and SVG drawing.
class FreeTypeFaceRec {
public:
  std::unique_ptr<FT_FaceRec, FreeTypeFaceDeleter> face;
  std::unique_ptr<FontFileStream> stream;

  // Will return nullptr on failure. Caller must lock FreeTypeMutex() before
  // calling this function.
  static std::unique_ptr<FreeTypeFaceRec> Make(const FontFace* typeface);
  ~FreeTypeFaceRec();

private:
  explicit FreeTypeFaceRec(std::unique_ptr<FontFileStream> file_stream);
  void SetupAxes(const FontFace& typeface);

  // Private to RefFreeTypeLibrary and UnrefFreeTypeLibrary.
  static int ft_count_;

  // Caller must lock FreeTypeMutex() before calling this function.
  static void RefFreeTypeLibrary();
  // Caller must lock FreeTypeMutex() before calling this function.
  static void UnrefFreeTypeLibrary();
};

// SkTypeface_FreeType::onFilterRec.
void FilterRecFreeType(ScalerContextRec* rec);

// SkTypeface_FreeType::onCreateScalerContext: falls back to
// SkScalerContext::MakeEmpty when FreeType cannot set up the face.
std::unique_ptr<ScalerContext> CreateScalerContextFreeType(std::shared_ptr<FontFace> typeface, const ScalerContextRec& rec);

} // namespace bkfont
