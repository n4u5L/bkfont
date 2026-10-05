// Ported from: skia/src/ports/SkFontMgr_custom.h

#pragma once

#include <memory>

#include "base/vector.h"
#include "font_manager.h"
#include "typeface_freetype.h"

namespace bkfont {

// SkTypeface_Custom. The base Typeface implementation for the custom font
// manager.
class TypefaceCustom : public TypefaceFreeType {
public:
  TypefaceCustom(const FontStyle& style, bool is_fixed_pitch,
                 bool sys_font, String family_name, int index);
  bool IsSysFont() const;

protected:
  void OnGetFamilyName(String* family_name) const override;
  int GetIndex() const;

private:
  const bool is_sys_font_;
  const String family_name_;
  const int index_;
};

// SkTypeface_Empty. The empty Typeface implementation for the custom font
// manager. Used as the last resort fallback typeface.
class TypefaceEmpty : public TypefaceCustom {
public:
  TypefaceEmpty();

protected:
  std::unique_ptr<StreamAsset> OnOpenStream(int*) const override;
  std::shared_ptr<Typeface> OnMakeClone(const FontArguments& args) const override;
  std::unique_ptr<FontStreamData> OnMakeFontData() const override;
};

// SkTypeface_File is not ported: only the directory loader makes one.

// SkFontStyleSet_Custom. This class is used by FontManagerCustom to hold
// TypefaceCustom families.
class FontStyleSetCustom : public FontStyleSet {
public:
  explicit FontStyleSetCustom(String family_name);

  // Should only be called during the initial build phase.
  void AppendTypeface(std::shared_ptr<Typeface> typeface);
  int Count() override;
  void GetStyle(int index, FontStyle* style, String* name) override;
  std::shared_ptr<Typeface> CreateTypeface(int index) override;
  std::shared_ptr<Typeface> MatchStyle(const FontStyle& pattern) override;
  String GetFamilyName();

private:
  Vector<std::shared_ptr<Typeface>> styles_;
  String family_name_;

  friend class FontManagerCustom;
};

// SkFontMgr_Custom. This class is essentially a collection of
// FontStyleSetCustom, one FontStyleSetCustom for each family. This class may
// be modified to load fonts from any source by changing the initialization.
class FontManagerCustom : public FontManager {
public:
  using Families = Vector<std::shared_ptr<FontStyleSetCustom>>;
  class SystemFontLoader {
  public:
    virtual ~SystemFontLoader() = default;
    // The SkFontScanner argument is not ported; no ported loader uses it.
    virtual void LoadSystemFonts(Families*) const = 0;
  };
  explicit FontManagerCustom(const SystemFontLoader& loader);

protected:
  int OnCountFamilies() const override;
  void OnGetFamilyName(int index, String* family_name) const override;
  std::shared_ptr<FontStyleSet> OnCreateStyleSet(int index) const override;
  std::shared_ptr<FontStyleSet> OnMatchFamily(const String& family_name) const override;
  std::shared_ptr<Typeface> OnMatchFamilyStyle(const String& family_name, const FontStyle& font_style) const override;
  std::shared_ptr<Typeface> OnMatchFamilyStyleCharacter(const String& family_name, const FontStyle&,
                                                        std::span<const String> bcp47, std::int32_t character) const override;
  std::shared_ptr<Typeface> OnMakeFromData(std::shared_ptr<Data> data, int ttc_index) const override;
  std::shared_ptr<Typeface> OnMakeFromStreamIndex(std::unique_ptr<StreamAsset>, int ttc_index) const override;
  std::shared_ptr<Typeface> OnMakeFromStreamArgs(std::unique_ptr<StreamAsset>, const FontArguments&) const override;
  std::shared_ptr<Typeface> OnMakeFromFile(const String& path, int ttc_index) const override;
  std::shared_ptr<Typeface> OnLegacyMakeTypeface(const String& family_name, FontStyle style) const override;

private:
  Families families_;
  std::shared_ptr<FontStyleSet> default_family_;
};

// SkFontMgr_New_Custom_Empty.
std::shared_ptr<FontManager> MakeFontManagerCustomEmpty();

} // namespace bkfont
