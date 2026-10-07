// Ported from: skia/include/core/SkFontMgr.h

#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "base/text/wtf_string.h"
#include "base/vector.h"
#include "data.h"
#include "font_arguments.h"
#include "font_style.h"
#include "stream.h"
#include "typeface.h"

namespace bkit {

// SkFontStyleSet.
class FontStyleSet {
public:
  FontStyleSet() = default;
  virtual ~FontStyleSet() = default;
  FontStyleSet(const FontStyleSet&) = delete;
  FontStyleSet& operator=(const FontStyleSet&) = delete;

  virtual int Count() = 0;
  virtual void GetStyle(int index, FontStyle* style, String* style_name) = 0;
  virtual std::shared_ptr<Typeface> CreateTypeface(int index) = 0;
  virtual std::shared_ptr<Typeface> MatchStyle(const FontStyle& pattern) = 0;

  static std::shared_ptr<FontStyleSet> CreateEmpty();

protected:
  std::shared_ptr<Typeface> MatchStyleCSS3(const FontStyle& pattern);
};

// SkFontMgr. Family names are null where upstream passes a null char*, and
// bcp47 holds the locale stack.
class FontManager {
public:
  FontManager() = default;
  virtual ~FontManager() = default;
  FontManager(const FontManager&) = delete;
  FontManager& operator=(const FontManager&) = delete;

  int CountFamilies() const;
  String GetFamilyName(int index) const;
  // Local metadata extension for UI display/search. Does not change the
  // host-selected GetFamilyName or create/rasterize a typeface. Backends
  // without localized collection metadata return their host-selected name.
  Vector<Typeface::LocalizedString> GetFamilyNames(int index) const;
  std::shared_ptr<FontStyleSet> CreateStyleSet(int index) const;

  // Never returns null; will return an empty set if the name is not found.
  //
  // Passing null as the parameter will return the default system family.
  // Note that most systems don't have a default system family, so passing
  // null will often result in the empty set.
  //
  // It is possible that this will return a style set not accessible from
  // CreateStyleSet(int) due to hidden or auto-activated fonts.
  std::shared_ptr<FontStyleSet> MatchFamily(const String& family_name) const;

  // Find the closest matching typeface to the specified family name and
  // style and return a ref to it. Will return null if no 'good' match is
  // found.
  //
  // Passing null as the parameter for 'family_name' will return the default
  // system font.
  //
  // It is possible that this will return a style set not accessible from
  // CreateStyleSet(int) or MatchFamily(const String&) due to hidden or
  // auto-activated fonts.
  std::shared_ptr<Typeface> MatchFamilyStyle(const String& family_name, const FontStyle&) const;

  // Use the system fallback to find a typeface for the given character. Note
  // that bcp47 is a combination of ISO 639, 15924, and 3166-1 codes, so it is
  // fine to just pass a ISO 639 here.
  //
  // Will return null if no family can be found for the character in the
  // system fallback.
  //
  // Passing null as the parameter for 'family_name' will return the default
  // system font.
  //
  // bcp47[0] is the least significant fallback, bcp47[bcp47.size()-1] is the
  // most significant. If no specified bcp47 codes match, any font with the
  // requested character will be matched.
  std::shared_ptr<Typeface> MatchFamilyStyleCharacter(const String& family_name, const FontStyle&,
                                                      std::span<const String> bcp47, std::int32_t character) const;

  // Create a typeface for the specified data and TTC index (pass 0 for none)
  // or null if the data is not recognized.
  std::shared_ptr<Typeface> MakeFromData(std::shared_ptr<Data> data, int ttc_index = 0) const;

  // Create a typeface for the specified stream and TTC index (pass 0 for
  // none) or null if the stream is not recognized.
  std::shared_ptr<Typeface> MakeFromStream(std::unique_ptr<StreamAsset> stream, int ttc_index = 0) const;

  // Create a typeface for the specified stream and FontArguments or null if
  // the stream is not recognized.
  std::shared_ptr<Typeface> MakeFromStream(std::unique_ptr<StreamAsset> stream, const FontArguments& args) const;

  // Create a typeface for the specified fileName and TTC index (pass 0 for
  // none) or null if the file doesn't exist, or is not recognized.
  std::shared_ptr<Typeface> MakeFromFile(const String& path, int ttc_index = 0) const;

  std::shared_ptr<Typeface> LegacyMakeTypeface(const String& family_name, FontStyle style) const;

  // Get the empty font manager.
  static std::shared_ptr<FontManager> RefEmpty();

protected:
  virtual int OnCountFamilies() const = 0;
  virtual void OnGetFamilyName(int index, String* family_name) const = 0;
  virtual Vector<Typeface::LocalizedString> OnGetFamilyNames(int index) const;
  virtual std::shared_ptr<FontStyleSet> OnCreateStyleSet(int index) const = 0;

  // May return null if the name is not found.
  virtual std::shared_ptr<FontStyleSet> OnMatchFamily(const String& family_name) const = 0;

  virtual std::shared_ptr<Typeface> OnMatchFamilyStyle(const String& family_name, const FontStyle&) const = 0;
  virtual std::shared_ptr<Typeface> OnMatchFamilyStyleCharacter(const String& family_name, const FontStyle&,
                                                                std::span<const String> bcp47, std::int32_t character) const = 0;

  virtual std::shared_ptr<Typeface> OnMakeFromData(std::shared_ptr<Data>, int ttc_index) const = 0;
  virtual std::shared_ptr<Typeface> OnMakeFromStreamIndex(std::unique_ptr<StreamAsset>, int ttc_index) const = 0;
  virtual std::shared_ptr<Typeface> OnMakeFromStreamArgs(std::unique_ptr<StreamAsset>, const FontArguments&) const = 0;
  virtual std::shared_ptr<Typeface> OnMakeFromFile(const String& path, int ttc_index) const = 0;

  virtual std::shared_ptr<Typeface> OnLegacyMakeTypeface(const String& family_name, FontStyle) const = 0;
};

} // namespace bkit
