// Ported from: skia/src/ports/SkFontConfigInterface_direct.cpp
// Ported from: skia/src/ports/SkFontMgr_FontConfigInterface.cpp
// Ported from: skia/src/ports/SkFontConfigTypeface.h

// Copyright 2009-2015 Google Inc.
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "font_manager_fontconfig.h"

#include <cassert>
#include <cstring>
#include <iterator>
#include <list>
#include <string>
#include <strings.h>
#include <unistd.h>
#include <utility>

#include "base/mutex.h"
#include "base/notreached.h"
#include "fontconfig_util.h"
#include "scalar.h"
#include "typeface_cache.h"
#include "typeface_freetype.h"
#include "typeface_proxy.h"

namespace bkfont {

namespace {

// Equivalence classes, used to match the Liberation and other fonts
// with their metric-compatible replacements.  See the discussion in
// GetFontEquivClass().
enum FontEquivClass {
  kOther,
  kSans,
  kSerif,
  kMono,
  kSymbol,
  kPGothic,
  kGothic,
  kPMincho,
  kMincho,
  kSimsun,
  kNSimsun,
  kSimhei,
  kPMingLiU,
  kMingLiU,
  kPMingLiUHK,
  kMingLiUHK,
  kCambria,
  kCalibri,
};

// Match the font name against a whilelist of fonts, returning the equivalence
// class.
FontEquivClass GetFontEquivClass(const char* font_name) {
  // It would be nice for fontconfig to tell us whether a given suggested
  // replacement is a "strong" match (that is, an equivalent font) or
  // a "weak" match (that is, fontconfig's next-best attempt at finding a
  // substitute).  However, I played around with the fontconfig API for
  // a good few hours and could not make it reveal this information.
  //
  // So instead, we hardcode.  Initially this function emulated
  //   /etc/fonts/conf.d/30-metric-aliases.conf
  // from my Ubuntu system, but we're better off being very conservative.

  // Arimo, Tinos and Cousine are a set of fonts metric-compatible with
  // Arial, Times New Roman and Courier New  with a character repertoire
  // much larger than Liberation. Note that Cousine is metrically
  // compatible with Courier New, but the former is sans-serif while
  // the latter is serif.

  struct FontEquivMap {
    FontEquivClass clazz;
    const char name[40];
  };

  static const FontEquivMap kFontEquivMap[] = {
    {kSans, "Arial"},
    {kSans, "Arimo"},
    {kSans, "Liberation Sans"},

    {kSerif, "Times New Roman"},
    {kSerif, "Tinos"},
    {kSerif, "Liberation Serif"},

    {kMono, "Courier New"},
    {kMono, "Cousine"},
    {kMono, "Liberation Mono"},

    {kSymbol, "Symbol"},
    {kSymbol, "Symbol Neu"},

    // ＭＳ Ｐゴシック
    {kPGothic, "MS PGothic"},
    {kPGothic, "\xef\xbc\xad\xef\xbc\xb3 \xef\xbc\xb0"
                  "\xe3\x82\xb4\xe3\x82\xb7\xe3\x83\x83\xe3\x82\xaf"},
    {kPGothic, "Noto Sans CJK JP"},
    {kPGothic, "IPAPGothic"},
    {kPGothic, "MotoyaG04Gothic"},

    // ＭＳ ゴシック
    {kGothic, "MS Gothic"},
    {kGothic, "\xef\xbc\xad\xef\xbc\xb3 "
                  "\xe3\x82\xb4\xe3\x82\xb7\xe3\x83\x83\xe3\x82\xaf"},
    {kGothic, "Noto Sans Mono CJK JP"},
    {kGothic, "IPAGothic"},
    {kGothic, "MotoyaG04GothicMono"},

    // ＭＳ Ｐ明朝
    {kPMincho, "MS PMincho"},
    {kPMincho, "\xef\xbc\xad\xef\xbc\xb3 \xef\xbc\xb0"
                  "\xe6\x98\x8e\xe6\x9c\x9d"},
    {kPMincho, "Noto Serif CJK JP"},
    {kPMincho, "IPAPMincho"},
    {kPMincho, "MotoyaG04Mincho"},

    // ＭＳ 明朝
    {kMincho, "MS Mincho"},
    {kMincho, "\xef\xbc\xad\xef\xbc\xb3 \xe6\x98\x8e\xe6\x9c\x9d"},
    {kMincho, "Noto Serif CJK JP"},
    {kMincho, "IPAMincho"},
    {kMincho, "MotoyaG04MinchoMono"},

    // 宋体
    {kSimsun, "Simsun"},
    {kSimsun, "\xe5\xae\x8b\xe4\xbd\x93"},
    {kSimsun, "Noto Serif CJK SC"},
    {kSimsun, "MSung GB18030"},
    {kSimsun, "Song ASC"},

    // 新宋体
    {kNSimsun, "NSimsun"},
    {kNSimsun, "\xe6\x96\xb0\xe5\xae\x8b\xe4\xbd\x93"},
    {kNSimsun, "Noto Serif CJK SC"},
    {kNSimsun, "MSung GB18030"},
    {kNSimsun, "N Song ASC"},

    // 黑体
    {kSimhei, "Simhei"},
    {kSimhei, "\xe9\xbb\x91\xe4\xbd\x93"},
    {kSimhei, "Noto Sans CJK SC"},
    {kSimhei, "MYingHeiGB18030"},
    {kSimhei, "MYingHeiB5HK"},

    // 新細明體
    {kPMingLiU, "PMingLiU"},
    {kPMingLiU, "\xe6\x96\xb0\xe7\xb4\xb0\xe6\x98\x8e\xe9\xab\x94"},
    {kPMingLiU, "Noto Serif CJK TC"},
    {kPMingLiU, "MSung B5HK"},

    // 細明體
    {kMingLiU, "MingLiU"},
    {kMingLiU, "\xe7\xb4\xb0\xe6\x98\x8e\xe9\xab\x94"},
    {kMingLiU, "Noto Serif CJK TC"},
    {kMingLiU, "MSung B5HK"},

    // 新細明體
    {kPMingLiUHK, "PMingLiU_HKSCS"},
    {kPMingLiUHK, "\xe6\x96\xb0\xe7\xb4\xb0\xe6\x98\x8e\xe9\xab\x94_HKSCS"},
    {kPMingLiUHK, "Noto Serif CJK TC"},
    {kPMingLiUHK, "MSung B5HK"},

    // 細明體
    {kMingLiUHK, "MingLiU_HKSCS"},
    {kMingLiUHK, "\xe7\xb4\xb0\xe6\x98\x8e\xe9\xab\x94_HKSCS"},
    {kMingLiUHK, "Noto Serif CJK TC"},
    {kMingLiUHK, "MSung B5HK"},

    // Cambria
    {kCambria, "Cambria"},
    {kCambria, "Caladea"},

    // Calibri
    {kCalibri, "Calibri"},
    {kCalibri, "Carlito"},
  };

  static const std::size_t kFontCount = std::size(kFontEquivMap);

  // TODO(jungshik): If this loop turns out to be hot, turn
  // the array to a static (hash)map to speed it up.
  for (std::size_t i = 0; i < kFontCount; ++i) {
    if (strcasecmp(kFontEquivMap[i].name, font_name) == 0)
      return kFontEquivMap[i].clazz;
  }
  return kOther;
}

// Return true if |font_a| and |font_b| are visually and at the metrics
// level interchangeable.
bool IsMetricCompatibleReplacement(const char* font_a, const char* font_b) {
  FontEquivClass class_a = GetFontEquivClass(font_a);
  FontEquivClass class_b = GetFontEquivClass(font_b);

  return class_a != kOther && class_a == class_b;
}

// Normally we only return exactly the font asked for. In last-resort
// cases, the request either doesn't specify a font or is one of the
// basic font names like "Sans", "Serif" or "Monospace". This function
// tells you whether a given request is for such a fallback.
bool IsFallbackFontAllowed(const std::string& family) {
  const char* family_cstr = family.c_str();
  return family.empty() ||
         strcasecmp(family_cstr, "sans") == 0 ||
         strcasecmp(family_cstr, "serif") == 0 ||
         strcasecmp(family_cstr, "monospace") == 0;
}

static int MapRange(float value, float old_min, float old_max, float new_min, float new_max) {
  assert(old_min < old_max);
  assert(new_min <= new_max);
  return new_min + ((value - old_min) * (new_max - new_min) / (old_max - old_min));
}

struct MapRanges {
  float old_val;
  float new_val;
};

static float MapValues(float val, MapRanges const ranges[], int range_count) {
  // -Inf to [0]
  if (val < ranges[0].old_val) {
    return ranges[0].new_val;
  }

  // Linear from [i] to [i+1]
  for (int i = 0; i < range_count - 1; ++i) {
    if (val < ranges[i + 1].old_val) {
      return MapRange(val, ranges[i].old_val, ranges[i + 1].old_val,
                      ranges[i].new_val, ranges[i + 1].new_val);
    }
  }

  // From [n] to +Inf
  // if (fcweight < Inf)
  return ranges[range_count - 1].new_val;
}

#ifndef FC_WEIGHT_DEMILIGHT
#define FC_WEIGHT_DEMILIGHT        65
#endif

// Available since FontConfig 2.15.
#ifndef FC_FONT_WRAPPER
#define FC_FONT_WRAPPER         "fontwrapper"
#endif

static FontStyle FontStyleFromPattern(FcPattern* pattern) {
  using FS = FontStyle;

  static constexpr MapRanges weight_ranges[] = {
    {FC_WEIGHT_THIN,       FS::kThin_Weight},
    {FC_WEIGHT_EXTRALIGHT, FS::kExtraLight_Weight},
    {FC_WEIGHT_LIGHT,      FS::kLight_Weight},
    {FC_WEIGHT_DEMILIGHT,  350},
    {FC_WEIGHT_BOOK,       380},
    {FC_WEIGHT_REGULAR,    FS::kNormal_Weight},
    {FC_WEIGHT_MEDIUM,     FS::kMedium_Weight},
    {FC_WEIGHT_DEMIBOLD,   FS::kSemiBold_Weight},
    {FC_WEIGHT_BOLD,       FS::kBold_Weight},
    {FC_WEIGHT_EXTRABOLD,  FS::kExtraBold_Weight},
    {FC_WEIGHT_BLACK,      FS::kBlack_Weight},
    {FC_WEIGHT_EXTRABLACK, FS::kExtraBlack_Weight},
  };
  float weight = MapValues(GetFontconfigInteger(pattern, FC_WEIGHT, FC_WEIGHT_REGULAR), weight_ranges, std::size(weight_ranges));

  static constexpr MapRanges width_ranges[] = {
    {FC_WIDTH_ULTRACONDENSED, FS::kUltraCondensed_Width},
    {FC_WIDTH_EXTRACONDENSED, FS::kExtraCondensed_Width},
    {FC_WIDTH_CONDENSED,      FS::kCondensed_Width},
    {FC_WIDTH_SEMICONDENSED,  FS::kSemiCondensed_Width},
    {FC_WIDTH_NORMAL,         FS::kNormal_Width},
    {FC_WIDTH_SEMIEXPANDED,   FS::kSemiExpanded_Width},
    {FC_WIDTH_EXPANDED,       FS::kExpanded_Width},
    {FC_WIDTH_EXTRAEXPANDED,  FS::kExtraExpanded_Width},
    {FC_WIDTH_ULTRAEXPANDED,  FS::kUltraExpanded_Width},
  };
  float width = MapValues(GetFontconfigInteger(pattern, FC_WIDTH, FC_WIDTH_NORMAL), width_ranges, std::size(width_ranges));

  FS::Slant slant = FS::kUpright_Slant;
  switch (GetFontconfigInteger(pattern, FC_SLANT, FC_SLANT_ROMAN)) {
  case FC_SLANT_ROMAN:
    slant = FS::kUpright_Slant;
    break;
  case FC_SLANT_ITALIC:
    slant = FS::kItalic_Slant;
    break;
  case FC_SLANT_OBLIQUE:
    slant = FS::kOblique_Slant;
    break;
  default:
    assert(false);
    break;
  }

  return FontStyle(FloatRoundToInt(weight), FloatRoundToInt(width), slant);
}

static void PatternFromFontStyle(FontStyle style, FcPattern* pattern) {
  using FS = FontStyle;

  static constexpr MapRanges weight_ranges[] = {
    {FS::kThin_Weight,       FC_WEIGHT_THIN},
    {FS::kExtraLight_Weight, FC_WEIGHT_EXTRALIGHT},
    {FS::kLight_Weight,      FC_WEIGHT_LIGHT},
    {350,                      FC_WEIGHT_DEMILIGHT},
    {380,                      FC_WEIGHT_BOOK},
    {FS::kNormal_Weight,     FC_WEIGHT_REGULAR},
    {FS::kMedium_Weight,     FC_WEIGHT_MEDIUM},
    {FS::kSemiBold_Weight,   FC_WEIGHT_DEMIBOLD},
    {FS::kBold_Weight,       FC_WEIGHT_BOLD},
    {FS::kExtraBold_Weight,  FC_WEIGHT_EXTRABOLD},
    {FS::kBlack_Weight,      FC_WEIGHT_BLACK},
    {FS::kExtraBlack_Weight, FC_WEIGHT_EXTRABLACK},
  };
  int weight = MapValues(style.GetWeight(), weight_ranges, std::size(weight_ranges));

  static constexpr MapRanges width_ranges[] = {
    {FS::kUltraCondensed_Width, FC_WIDTH_ULTRACONDENSED},
    {FS::kExtraCondensed_Width, FC_WIDTH_EXTRACONDENSED},
    {FS::kCondensed_Width,      FC_WIDTH_CONDENSED},
    {FS::kSemiCondensed_Width,  FC_WIDTH_SEMICONDENSED},
    {FS::kNormal_Width,         FC_WIDTH_NORMAL},
    {FS::kSemiExpanded_Width,   FC_WIDTH_SEMIEXPANDED},
    {FS::kExpanded_Width,       FC_WIDTH_EXPANDED},
    {FS::kExtraExpanded_Width,  FC_WIDTH_EXTRAEXPANDED},
    {FS::kUltraExpanded_Width,  FC_WIDTH_ULTRAEXPANDED},
  };
  int width = MapValues(style.GetWidth(), width_ranges, std::size(width_ranges));

  int slant = FC_SLANT_ROMAN;
  switch (style.GetSlant()) {
  case FS::kUpright_Slant:
    slant = FC_SLANT_ROMAN;
    break;
  case FS::kItalic_Slant:
    slant = FC_SLANT_ITALIC;
    break;
  case FS::kOblique_Slant:
    slant = FC_SLANT_OBLIQUE;
    break;
  default:
    assert(false);
    break;
  }

  FcPatternAddInteger(pattern, FC_WEIGHT, weight);
  FcPatternAddInteger(pattern, FC_WIDTH, width);
  FcPatternAddInteger(pattern, FC_SLANT, slant);
}

struct FontIdentity {
  std::string filename;
  int ttc_index = 0;
  bool operator==(const FontIdentity&) const = default;
};

bool IsValidPattern(FcConfig* config, FcPattern* pattern) {
  // Chromium defines SK_FONT_CONFIG_INTERFACE_ONLY_ALLOW_SFNT_FONTS.
  const char* format = GetFontconfigString(pattern, FC_FONTFORMAT);
  if (!format || (std::strcmp(format, "TrueType") != 0 && std::strcmp(format, "CFF") != 0)) return false;
  const char* filename = GetFontconfigString(pattern, FC_FILE);
  if (!filename) return false;
  const char* sysroot = reinterpret_cast<const char*>(FcConfigGetSysRoot(config));
  const std::string resolved_filename = std::string(sysroot ? sysroot : "") + filename;
  return access(resolved_filename.c_str(), R_OK) == 0;
}

bool MatchFamilyName(const String& family_name, FontStyle style, FontIdentity* identity,
                     String* matched_family, FontStyle* matched_style) {
  const std::string family(family_name.Utf8().c_str());
  if (family.size() > 2048) return false;
  FontconfigLocker lock;
  FcConfig* config = GetGlobalFontConfig();
  FontconfigPattern pattern(FcPatternCreate());
  if (!family_name.IsNull()) {
    FcPatternAddString(pattern.get(), FC_FAMILY, reinterpret_cast<const FcChar8*>(family.c_str()));
  }
  PatternFromFontStyle(style, pattern.get());
  FcPatternAddBool(pattern.get(), FC_SCALABLE, FcTrue);
  FcPatternAddString(pattern.get(), FC_FONT_WRAPPER, reinterpret_cast<const FcChar8*>("SFNT"));
  FcConfigSubstitute(config, pattern.get(), FcMatchPattern);
  FcDefaultSubstitute(pattern.get());

  const char* post_config_family = GetFontconfigString(pattern.get(), FC_FAMILY);
  if (!post_config_family) post_config_family = "";
  FcResult result;
  FontconfigFontSet fonts(FcFontSort(config, pattern.get(), FcFalse, nullptr, &result));
  if (!fonts) return false;
  FcPattern* match = nullptr;
  for (int i = 0; i < fonts->nfont; ++i) {
    if (IsValidPattern(config, fonts->fonts[i])) {
      match = fonts->fonts[i];
      break;
    }
  }
  if (!match) return false;
  if (!IsFallbackFontAllowed(family)) {
    bool acceptable_substitute = false;
    for (int id = 0; id < 255; ++id) {
      const char* actual_family = GetFontconfigString(match, FC_FAMILY, id);
      if (!actual_family) break;
      acceptable_substitute = strcasecmp(post_config_family, actual_family) == 0 ||
                              strcasecmp(family.c_str(), actual_family) == 0 ||
                              IsMetricCompatibleReplacement(family.c_str(), actual_family);
      if (acceptable_substitute) break;
    }
    if (!acceptable_substitute) return false;
  }

  const char* actual_family = GetFontconfigString(match, FC_FAMILY);
  const char* filename = GetFontconfigString(match, FC_FILE);
  if (!actual_family || !filename) return false;
  const char* sysroot = reinterpret_cast<const char*>(FcConfigGetSysRoot(config));
  identity->filename = std::string(sysroot ? sysroot : "") + filename;
  identity->ttc_index = GetFontconfigInteger(match, FC_INDEX, 0);
  *matched_family = String::FromUTF8(actual_family);
  *matched_style = FontStyleFromPattern(match);
  return true;
}

class TypefaceFontconfig final : public TypefaceProxy {
public:
  TypefaceFontconfig(std::shared_ptr<Typeface> real_typeface, FontIdentity identity,
                     String family, FontStyle style, bool fixed_pitch = false)
      : TypefaceProxy(std::move(real_typeface), style, fixed_pitch),
        identity_(std::move(identity)),
        family_(std::move(family)) {
  }

  const FontIdentity& GetIdentity() const {
    return identity_;
  }

protected:
  std::unique_ptr<StreamAsset> OnOpenStream(int* ttc_index) const override {
    *ttc_index = identity_.ttc_index;
    return OpenFontconfigStream(identity_.filename);
  }
  std::shared_ptr<Typeface> OnMakeClone(const FontArguments& args) const override {
    auto real_typeface = TypefaceProxy::OnMakeClone(args);
    if (!real_typeface) return nullptr;
    return std::make_shared<TypefaceFontconfig>(std::move(real_typeface), identity_, family_, GetFontStyle(), IsFixedPitch());
  }
  void OnGetFamilyName(String* family_name) const override {
    *family_name = family_;
  }
  FontStyle OnGetFontStyle() const override {
    return Typeface::OnGetFontStyle();
  }
  bool OnGetFixedPitch() const override {
    return Typeface::OnGetFixedPitch();
  }

private:
  FontIdentity identity_;
  String family_;
};

// SkFontRequestCache's LRU and byte budget, expressed with standard containers.
// The key header has four uint32_t fields and a namespace pointer; the result
// charges one typeface pointer, as SkResourceCache::Rec::bytesUsed does.
class FontRequestCache {
public:
  std::shared_ptr<Typeface> Find(const std::string& family, FontStyle style) {
    for (auto it = requests_.begin(); it != requests_.end(); ++it) {
      if (it->family == family && it->style == style) {
        requests_.splice(requests_.begin(), requests_, it);
        return requests_.front().typeface;
      }
    }
    return nullptr;
  }
  void Add(std::string family, FontStyle style, std::shared_ptr<Typeface> typeface) {
    for (auto it = requests_.begin(); it != requests_.end(); ++it) {
      if (it->family == family && it->style == style) {
        bytes_used_ -= it->BytesUsed();
        requests_.erase(it);
        break;
      }
    }
    requests_.push_front({std::move(family), style, std::move(typeface)});
    bytes_used_ += requests_.front().BytesUsed();
    while (bytes_used_ >= (1 << 15)) {
      bytes_used_ -= requests_.back().BytesUsed();
      requests_.pop_back();
    }
  }

private:
  struct Request {
    std::string family;
    FontStyle style;
    std::shared_ptr<Typeface> typeface;
    std::size_t BytesUsed() const {
      return 4 * sizeof(std::uint32_t) + 2 * sizeof(void*) + sizeof(FontStyle) +
             ((family.size() + 3) & ~std::size_t{3});
    }
  };
  std::list<Request> requests_;
  std::size_t bytes_used_ = 0;
};

class FontManagerFontconfig final : public FontManager {
protected:
  // These entry points are deliberately unsupported by SkFontMgr_FCI.
  // Blink's Linux character fallback is handled by gfx::GetFallbackFontForChar.
  int OnCountFamilies() const override {
    NOTREACHED();
  }
  void OnGetFamilyName(int, String*) const override {
    NOTREACHED();
  }
  std::shared_ptr<FontStyleSet> OnCreateStyleSet(int) const override {
    NOTREACHED();
  }
  std::shared_ptr<FontStyleSet> OnMatchFamily(const String&) const override {
    NOTREACHED();
  }
  std::shared_ptr<Typeface> OnMatchFamilyStyleCharacter(const String&, const FontStyle&,
                                                      std::span<const String>, std::int32_t) const override {
    NOTREACHED();
  }
  std::shared_ptr<Typeface> OnMatchFamilyStyle(const String& family, const FontStyle& style) const override {
    AutoMutexExclusive lock(mutex_);
    const std::string request_name(family.Utf8().c_str());
    auto face = requests_.Find(request_name, style);
    if (face) return face;
    FontIdentity identity;
    String matched_family;
    FontStyle matched_style;
    if (!MatchFamilyName(family, style, &identity, &matched_family, &matched_style)) return nullptr;
    face = typefaces_.FindByProcAndRef([](Typeface* cached, void* context) {
      return static_cast<TypefaceFontconfig*>(cached)->GetIdentity() == *static_cast<FontIdentity*>(context);
    }, &identity);
    if (!face) {
      auto stream = OpenFontconfigStream(identity.filename);
      if (stream) {
        auto real_typeface = TypefaceFreeType::MakeFromStream(
            std::move(stream), FontArguments().SetCollectionIndex(identity.ttc_index));
        if (real_typeface) {
          face = std::make_shared<TypefaceFontconfig>(std::move(real_typeface), identity, matched_family, matched_style);
          typefaces_.Add(face);
        }
      }
    }
    requests_.Add(request_name, style, face);
    return face;
  }
  std::shared_ptr<Typeface> OnMakeFromData(std::shared_ptr<Data> data, int ttc_index) const override {
    return OnMakeFromStreamIndex(MemoryStream::Make(std::move(data)), ttc_index);
  }
  std::shared_ptr<Typeface> OnMakeFromStreamIndex(std::unique_ptr<StreamAsset> stream, int ttc_index) const override {
    return MakeFromStream(std::move(stream), FontArguments().SetCollectionIndex(ttc_index));
  }
  std::shared_ptr<Typeface> OnMakeFromStreamArgs(std::unique_ptr<StreamAsset> stream, const FontArguments& args) const override {
    const std::size_t length = stream->GetLength();
    if (!length || length >= 1024 * 1024 * 1024) return nullptr;
    return TypefaceFreeType::MakeFromStream(std::move(stream), args);
  }
  std::shared_ptr<Typeface> OnMakeFromFile(const String& path, int ttc_index) const override {
    auto stream = Stream::MakeFromFile(path);
    return stream ? MakeFromStream(std::move(stream), ttc_index) : nullptr;
  }
  std::shared_ptr<Typeface> OnLegacyMakeTypeface(const String& family, FontStyle style) const override {
    return OnMatchFamilyStyle(family, style);
  }

private:
  mutable Mutex mutex_;
  mutable TypefaceCache typefaces_;
  mutable FontRequestCache requests_;
};

} // namespace

std::shared_ptr<FontManager> MakeFontManagerFontconfig() {
  // skia::DefaultFontMgr retains its instance for the lifetime of the process.
  static const auto& manager = *new std::shared_ptr<FontManager>(std::make_shared<FontManagerFontconfig>());
  return manager;
}

} // namespace bkfont
