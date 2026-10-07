// Ported from: skia/src/core/SkFontMgr.cpp

#include "font_manager.h"

#include <utility>

namespace bkit {

namespace {

class EmptyFontStyleSet final : public FontStyleSet {
public:
  int Count() override {
    return 0;
  }
  void GetStyle(int, FontStyle*, String*) override {
  }
  std::shared_ptr<Typeface> CreateTypeface(int) override {
    return nullptr;
  }
  std::shared_ptr<Typeface> MatchStyle(const FontStyle&) override {
    return nullptr;
  }
};

class EmptyFontManager final : public FontManager {
protected:
  int OnCountFamilies() const override {
    return 0;
  }
  void OnGetFamilyName(int, String*) const override {
  }
  std::shared_ptr<FontStyleSet> OnCreateStyleSet(int) const override {
    return nullptr;
  }
  std::shared_ptr<FontStyleSet> OnMatchFamily(const String&) const override {
    return FontStyleSet::CreateEmpty();
  }

  std::shared_ptr<Typeface> OnMatchFamilyStyle(const String&, const FontStyle&) const override {
    return nullptr;
  }
  std::shared_ptr<Typeface> OnMatchFamilyStyleCharacter(const String&, const FontStyle&,
                                                        std::span<const String>, std::int32_t) const override {
    return nullptr;
  }

  std::shared_ptr<Typeface> OnMakeFromData(std::shared_ptr<Data>, int) const override {
    return nullptr;
  }
  std::shared_ptr<Typeface> OnMakeFromStreamIndex(std::unique_ptr<StreamAsset>, int) const override {
    return nullptr;
  }
  std::shared_ptr<Typeface> OnMakeFromStreamArgs(std::unique_ptr<StreamAsset>, const FontArguments&) const override {
    return nullptr;
  }
  std::shared_ptr<Typeface> OnMakeFromFile(const String&, int) const override {
    return nullptr;
  }
  std::shared_ptr<Typeface> OnLegacyMakeTypeface(const String&, FontStyle) const override {
    return nullptr;
  }
};

std::shared_ptr<FontStyleSet> EmptyOnNull(std::shared_ptr<FontStyleSet>&& fsset) {
  if (!fsset) {
    fsset = FontStyleSet::CreateEmpty();
  }
  return std::move(fsset);
}

} // namespace

std::shared_ptr<FontStyleSet> FontStyleSet::CreateEmpty() {
  return std::make_shared<EmptyFontStyleSet>();
}

int FontManager::CountFamilies() const {
  return OnCountFamilies();
}

String FontManager::GetFamilyName(int index) const {
  String family_name;
  OnGetFamilyName(index, &family_name);
  return family_name;
}

Vector<Typeface::LocalizedString> FontManager::GetFamilyNames(int index) const {
  return OnGetFamilyNames(index);
}

Vector<Typeface::LocalizedString> FontManager::OnGetFamilyNames(int index) const {
  Vector<Typeface::LocalizedString> names;
  String name = GetFamilyName(index);
  if (!name.empty()) names.push_back(Typeface::LocalizedString{std::move(name), String()});
  return names;
}

std::shared_ptr<FontStyleSet> FontManager::CreateStyleSet(int index) const {
  return EmptyOnNull(OnCreateStyleSet(index));
}

std::shared_ptr<FontStyleSet> FontManager::MatchFamily(const String& family_name) const {
  return EmptyOnNull(OnMatchFamily(family_name));
}

std::shared_ptr<Typeface> FontManager::MatchFamilyStyle(const String& family_name, const FontStyle& fs) const {
  return OnMatchFamilyStyle(family_name, fs);
}

std::shared_ptr<Typeface> FontManager::MatchFamilyStyleCharacter(const String& family_name, const FontStyle& style,
                                                                 std::span<const String> bcp47, std::int32_t character) const {
  return OnMatchFamilyStyleCharacter(family_name, style, bcp47, character);
}

std::shared_ptr<Typeface> FontManager::MakeFromData(std::shared_ptr<Data> data, int ttc_index) const {
  if (nullptr == data) {
    return nullptr;
  }
  return OnMakeFromData(std::move(data), ttc_index);
}

std::shared_ptr<Typeface> FontManager::MakeFromStream(std::unique_ptr<StreamAsset> stream, int ttc_index) const {
  if (nullptr == stream) {
    return nullptr;
  }
  return OnMakeFromStreamIndex(std::move(stream), ttc_index);
}

std::shared_ptr<Typeface> FontManager::MakeFromStream(std::unique_ptr<StreamAsset> stream, const FontArguments& args) const {
  if (nullptr == stream) {
    return nullptr;
  }
  return OnMakeFromStreamArgs(std::move(stream), args);
}

std::shared_ptr<Typeface> FontManager::MakeFromFile(const String& path, int ttc_index) const {
  if (path.IsNull()) {
    return nullptr;
  }
  return OnMakeFromFile(path, ttc_index);
}

std::shared_ptr<Typeface> FontManager::LegacyMakeTypeface(const String& family_name, FontStyle style) const {
  return OnLegacyMakeTypeface(family_name, style);
}

std::shared_ptr<FontManager> FontManager::RefEmpty() {
  static const std::shared_ptr<FontManager>& singleton = *new std::shared_ptr<FontManager>(std::make_shared<EmptyFontManager>());
  return singleton;
}

// Width has the greatest priority.
// If the value of pattern.width is 5 (normal) or less,
//    narrower width values are checked first, then wider values.
// If the value of pattern.width is greater than 5 (normal),
//    wider values are checked first, followed by narrower values.
//
// Italic/Oblique has the next highest priority.
// If italic requested and there is some italic font, use it.
// If oblique requested and there is some oblique font, use it.
// If italic requested and there is some oblique font, use it.
// If oblique requested and there is some italic font, use it.
//
// Exact match.
// If pattern.weight < 400, weights below pattern.weight are checked
//   in descending order followed by weights above pattern.weight
//   in ascending order until a match is found.
// If pattern.weight > 500, weights above pattern.weight are checked
//   in ascending order followed by weights below pattern.weight
//   in descending order until a match is found.
// If pattern.weight is 400, 500 is checked first
//   and then the rule for pattern.weight < 400 is used.
// If pattern.weight is 500, 400 is checked first
//   and then the rule for pattern.weight < 400 is used.
std::shared_ptr<Typeface> FontStyleSet::MatchStyleCSS3(const FontStyle& pattern) {
  int count = Count();
  if (0 == count) {
    return nullptr;
  }

  struct Score {
    int score;
    int index;
    Score& operator+=(int rhs) {
      score += rhs;
      return *this;
    }
    Score& operator<<=(int rhs) {
      score <<= rhs;
      return *this;
    }
    bool operator<(const Score& that) const {
      return score < that.score;
    }
  };

  Score max_score = {0, 0};
  for (int i = 0; i < count; ++i) {
    FontStyle current;
    GetStyle(i, &current, nullptr);
    Score current_score = {0, i};

    // CSS stretch / FontStyle::Width
    // Takes priority over everything else.
    if (pattern.GetWidth() <= FontStyle::kNormal_Width) {
      if (current.GetWidth() <= pattern.GetWidth()) {
        current_score += 10 - pattern.GetWidth() + current.GetWidth();
      } else {
        current_score += 10 - current.GetWidth();
      }
    } else {
      if (current.GetWidth() > pattern.GetWidth()) {
        current_score += 10 + pattern.GetWidth() - current.GetWidth();
      } else {
        current_score += current.GetWidth();
      }
    }
    current_score <<= 8;

    // CSS style (normal, italic, oblique) / FontStyle::Slant (upright, italic,
    // oblique). Takes priority over all valid weights.
    static_assert(FontStyle::kUpright_Slant == 0 &&
                      FontStyle::kItalic_Slant == 1 &&
                      FontStyle::kOblique_Slant == 2,
                  "FontStyle::Slant values not as required.");
    static const int kScore[3][3] = {
        /*               Upright Italic Oblique  [current]*/
        /*   Upright */ {3, 1, 2},
        /*   Italic  */ {1, 3, 2},
        /*   Oblique */ {1, 2, 3},
        /* [pattern] */
    };
    current_score += kScore[pattern.GetSlant()][current.GetSlant()];
    current_score <<= 8;

    // Synthetics (weight, style) [no stretch synthetic?]

    // CSS weight / FontStyle::Weight
    // The 'closer' to the target weight, the higher the score.
    // 1000 is the 'heaviest' recognized weight
    if (pattern.GetWeight() == current.GetWeight()) {
      current_score += 1000;
      // less than 400 prefer lighter weights
    } else if (pattern.GetWeight() < 400) {
      if (current.GetWeight() <= pattern.GetWeight()) {
        current_score += 1000 - pattern.GetWeight() + current.GetWeight();
      } else {
        current_score += 1000 - current.GetWeight();
      }
      // between 400 and 500 prefer heavier up to 500, then lighter weights
    } else if (pattern.GetWeight() <= 500) {
      if (current.GetWeight() >= pattern.GetWeight() && current.GetWeight() <= 500) {
        current_score += 1000 + pattern.GetWeight() - current.GetWeight();
      } else if (current.GetWeight() <= pattern.GetWeight()) {
        current_score += 500 + current.GetWeight();
      } else {
        current_score += 1000 - current.GetWeight();
      }
      // greater than 500 prefer heavier weights
    } else if (pattern.GetWeight() > 500) {
      if (current.GetWeight() > pattern.GetWeight()) {
        current_score += 1000 + pattern.GetWeight() - current.GetWeight();
      } else {
        current_score += current.GetWeight();
      }
    }

    if (max_score < current_score) {
      max_score = current_score;
    }
  }

  return CreateTypeface(max_score.index);
}

} // namespace bkit
