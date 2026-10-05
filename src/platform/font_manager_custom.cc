// Ported from: skia/src/ports/SkFontMgr_custom.cpp

#include "font_manager_custom.h"

#include <iterator>
#include <utility>

namespace bkfont {

namespace {

// SkString::equals(const char*) treats a null argument as "".
bool FamilyNameEquals(const String& name, const String& family_name) {
  const String& a = name.IsNull() ? g_empty_string : name;
  const String& b = family_name.IsNull() ? g_empty_string : family_name;
  return a == b;
}

} // namespace

TypefaceCustom::TypefaceCustom(const FontStyle& style, bool is_fixed_pitch,
                               bool sys_font, String family_name, int index)
    : TypefaceFreeType(style, is_fixed_pitch),
      is_sys_font_(sys_font),
      family_name_(std::move(family_name)),
      index_(index) {
}

bool TypefaceCustom::IsSysFont() const {
  return is_sys_font_;
}

void TypefaceCustom::OnGetFamilyName(String* family_name) const {
  *family_name = family_name_;
}

int TypefaceCustom::GetIndex() const {
  return index_;
}

TypefaceEmpty::TypefaceEmpty()
    : TypefaceCustom(FontStyle(), false, true, String(""), 0) {
}

std::unique_ptr<StreamAsset> TypefaceEmpty::OnOpenStream(int*) const {
  return nullptr;
}

std::shared_ptr<Typeface> TypefaceEmpty::OnMakeClone(const FontArguments&) const {
  return RefThis();
}

std::unique_ptr<FontStreamData> TypefaceEmpty::OnMakeFontData() const {
  return nullptr;
}

FontStyleSetCustom::FontStyleSetCustom(String family_name)
    : family_name_(std::move(family_name)) {
}

void FontStyleSetCustom::AppendTypeface(std::shared_ptr<Typeface> typeface) {
  styles_.push_back(std::move(typeface));
}

int FontStyleSetCustom::Count() {
  return static_cast<int>(styles_.size());
}

void FontStyleSetCustom::GetStyle(int index, FontStyle* style, String* name) {
  if (style) {
    *style = styles_[static_cast<wtf_size_t>(index)]->GetFontStyle();
  }
  if (name) {
    *name = String("");
  }
}

std::shared_ptr<Typeface> FontStyleSetCustom::CreateTypeface(int index) {
  return styles_[static_cast<wtf_size_t>(index)];
}

std::shared_ptr<Typeface> FontStyleSetCustom::MatchStyle(const FontStyle& pattern) {
  return MatchStyleCSS3(pattern);
}

String FontStyleSetCustom::GetFamilyName() {
  return family_name_;
}

FontManagerCustom::FontManagerCustom(const SystemFontLoader& loader)
    : default_family_(nullptr) {
  loader.LoadSystemFonts(&families_);

  // Try to pick a default font. The trailing null name matches the family
  // whose name is empty, as SkString::equals(nullptr) does.
  static const char* kDefaultNames[] = {
      "Arial", "Verdana", "Times New Roman", "Droid Sans", "DejaVu Serif", nullptr};
  for (std::size_t i = 0; i < std::size(kDefaultNames); ++i) {
    std::shared_ptr<FontStyleSet> set(OnMatchFamily(kDefaultNames[i] ? String(kDefaultNames[i]) : String()));
    if (nullptr == set) {
      continue;
    }

    std::shared_ptr<Typeface> tf(set->MatchStyle(FontStyle(FontStyle::kNormal_Weight,
                                                           FontStyle::kNormal_Width,
                                                           FontStyle::kUpright_Slant)));
    if (nullptr == tf) {
      continue;
    }

    default_family_ = set;
    break;
  }
  if (nullptr == default_family_) {
    default_family_ = families_[0];
  }
}

int FontManagerCustom::OnCountFamilies() const {
  return static_cast<int>(families_.size());
}

void FontManagerCustom::OnGetFamilyName(int index, String* family_name) const {
  *family_name = families_[static_cast<wtf_size_t>(index)]->GetFamilyName();
}

std::shared_ptr<FontStyleSet> FontManagerCustom::OnCreateStyleSet(int index) const {
  return families_[static_cast<wtf_size_t>(index)];
}

std::shared_ptr<FontStyleSet> FontManagerCustom::OnMatchFamily(const String& family_name) const {
  for (wtf_size_t i = 0; i < families_.size(); ++i) {
    if (FamilyNameEquals(families_[i]->GetFamilyName(), family_name)) {
      return families_[i];
    }
  }
  return nullptr;
}

std::shared_ptr<Typeface> FontManagerCustom::OnMatchFamilyStyle(const String& family_name,
                                                                const FontStyle& font_style) const {
  std::shared_ptr<FontStyleSet> sset(MatchFamily(family_name));
  return sset->MatchStyle(font_style);
}

std::shared_ptr<Typeface> FontManagerCustom::OnMatchFamilyStyleCharacter(const String&, const FontStyle&,
                                                                         std::span<const String>, std::int32_t) const {
  return nullptr;
}

std::shared_ptr<Typeface> FontManagerCustom::OnMakeFromData(std::shared_ptr<Data> data, int ttc_index) const {
  return MakeFromStream(std::make_unique<MemoryStream>(std::move(data)), ttc_index);
}

std::shared_ptr<Typeface> FontManagerCustom::OnMakeFromStreamIndex(std::unique_ptr<StreamAsset> stream,
                                                                   int ttc_index) const {
  return MakeFromStream(std::move(stream), FontArguments().SetCollectionIndex(ttc_index));
}

std::shared_ptr<Typeface> FontManagerCustom::OnMakeFromStreamArgs(std::unique_ptr<StreamAsset> stream,
                                                                  const FontArguments& args) const {
  return TypefaceFreeType::MakeFromStream(std::move(stream), args);
}

std::shared_ptr<Typeface> FontManagerCustom::OnMakeFromFile(const String& path, int ttc_index) const {
  std::unique_ptr<StreamAsset> stream = Stream::MakeFromFile(path);
  return stream ? MakeFromStream(std::move(stream), ttc_index) : nullptr;
}

std::shared_ptr<Typeface> FontManagerCustom::OnLegacyMakeTypeface(const String& family_name,
                                                                  FontStyle style) const {
  std::shared_ptr<Typeface> tf;

  if (!family_name.IsNull()) {
    tf = OnMatchFamilyStyle(family_name, style);
  }

  if (!tf) {
    tf = default_family_->MatchStyle(style);
  }

  return tf;
}

namespace {

// EmptyFontLoader from skia/src/ports/SkFontMgr_custom_empty.cpp.
class EmptyFontLoader final : public FontManagerCustom::SystemFontLoader {
public:
  EmptyFontLoader() = default;

  void LoadSystemFonts(FontManagerCustom::Families* families) const override {
    std::shared_ptr<FontStyleSetCustom> family = std::make_shared<FontStyleSetCustom>(String(""));
    families->push_back(family);
    family->AppendTypeface(std::make_shared<TypefaceEmpty>());
  }
};

} // namespace

std::shared_ptr<FontManager> MakeFontManagerCustomEmpty() {
  return std::make_shared<FontManagerCustom>(EmptyFontLoader());
}

} // namespace bkfont
