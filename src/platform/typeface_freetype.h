// Ported from: skia/src/ports/SkTypeface_FreeType.h

#pragma once

#include <cstdint>
#include <memory>

#include "base/mutex.h"
#include "base/once.h"
#include "font_descriptor.h"
#include "typeface.h"

typedef struct FT_FaceRec_* FT_Face;

namespace bkit {

// f_t_mutex. Caller must lock it before calling into FreeType.
Mutex& FreeTypeMutex();

// SkTypeface_FreeType.
class TypefaceFreeType : public Typeface {
public:
  // Fetch units/EM from "head" table if needed (ie for bitmap fonts)
  static int GetUnitsPerEm(FT_Face face);

  // Return the font data, or null on failure.
  std::unique_ptr<FontStreamData> MakeFontData() const;
  class FaceRec;
  // Caller must lock FreeTypeMutex() before calling this function.
  FaceRec* GetFaceRec() const;

  static std::shared_ptr<Typeface> MakeFromStream(std::unique_ptr<StreamAsset> stream, const FontArguments& args);

protected:
  TypefaceFreeType(const FontStyle& style, bool is_fixed_pitch);
  ~TypefaceFreeType() override;

  std::unique_ptr<FontStreamData> CloneFontData(const FontArguments& args, FontStyle* style) const;
  std::unique_ptr<ScalerContext> OnCreateScalerContext(const ScalerContextRec& rec) const override;
  std::unique_ptr<ScalerContext> OnCreateScalerContextAsProxyTypeface(const ScalerContextRec& rec, Typeface* proxy_typeface) const override;
  void OnFilterRec(ScalerContextRec* rec) const override;
  bool OnGetPostScriptName(String* post_script_name) const override;
  int OnGetUPEM() const override;
  void OnCharsToGlyphs(std::span<const std::int32_t> unis, std::span<std::uint16_t> glyphs) const override;
  int OnCountGlyphs() const override;

  std::unique_ptr<LocalizedStrings> OnCreateFamilyNameIterator() const override;

  bool OnGlyphMaskNeedsCurrentColor() const override;
  int OnGetVariationDesignPosition(std::span<FontArguments::VariationPosition::Coordinate> coordinates) const override;
  int OnGetVariationDesignParameters(std::span<FontParameters::Variation::Axis> parameters) const override;
  int OnGetTableTags(std::span<std::uint32_t> tags) const override;
  std::size_t OnGetTableData(std::uint32_t tag, std::size_t offset, std::size_t length, void* data) const override;
  std::shared_ptr<Data> OnCopyTableData(std::uint32_t tag) const override;

  virtual std::unique_ptr<FontStreamData> OnMakeFontData() const = 0;

private:
  mutable Once ft_face_once_;
  mutable std::unique_ptr<FaceRec> face_rec_;

  // The SkCharToGlyphCache in front of FT_Get_Char_Index is not ported. It
  // only memoizes the lookup.

  mutable Once glyph_masks_may_need_current_color_once_;
  mutable bool glyph_masks_may_need_current_color_ = false;
};

// SkTypeface_FreeTypeStream.
class TypefaceFreeTypeStream : public TypefaceFreeType {
public:
  TypefaceFreeTypeStream(std::unique_ptr<FontStreamData> font_data, const String family_name,
                         const FontStyle& style, bool is_fixed_pitch);
  ~TypefaceFreeTypeStream() override;

protected:
  void OnGetFamilyName(String* family_name) const override;
  std::unique_ptr<StreamAsset> OnOpenStream(int* ttc_index) const override;
  std::unique_ptr<FontStreamData> OnMakeFontData() const override;
  std::shared_ptr<Typeface> OnMakeClone(const FontArguments& args) const override;

private:
  const String family_name_;
  const std::unique_ptr<const FontStreamData> data_;
};

} // namespace bkit
