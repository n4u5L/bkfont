// Multilingual inline layout example: the plot of Mushoku Tensei in Chinese,
// Japanese (vertical), Korean, English, Thai, Arabic and a few more scripts,
// styled with the CSS longhands the style system implements.
//
//   bkit_inline_text_render_example                 opens a scrollable window
//   bkit_inline_text_render_example --snapshot a.bmp renders the whole page
//   bkit_inline_text_render_example --render-scale 0.5 starts at half resolution
//   Keys: 1/2/3 = 100%/50%/25% resolution, F = nearest/linear magnification.
//   --render-scale also applies to snapshots (the BMP stores the raster pixels).
//
// There is no CSS parser: every declaration is built with StyleDeclaration's
// typed setters, the values the parser would produce. Each paragraph is its
// own InlineFormattingContext (one block container), and shared declarations
// are named rules in the context's StyleSheet.
//
// Above its rendering, each card shows the HTML that renders the same in a
// browser, serialized from the same typed inputs while the blocks are built.
// Drag over it to select; Ctrl+C copies the selection, or the whole snippet
// when the selection is collapsed, and Ctrl+A selects the snippet.
#include <GLFW/glfw3.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "base/hash_map.h"
#include "damage.h"
#include "font/custom_font_data.h"
#include "font/simple_font_data.h"
#include "fonts.h"
#include "inline_layout.h"
#include "paint/path.h"
#include "raster_tiles.h"
#include "style/css_identifier_value.h"
#include "style/css_numeric_literal_value.h"
#include "style/css_string_value.h"
#include "style/css_value_list.h"

namespace {

using namespace bkit;
using P = CSSPropertyID;
using V = CSSValueID;
using Unit = CSSPrimitiveValue::UnitType;
namespace L = css_longhand;
namespace S = css_shorthand;

constexpr int kWindowWidth = 1040;
constexpr int kWindowHeight = 820;
// Logical (CSS) pixels; multiplied by the device scale factor when painting.
constexpr float kPageMargin = 28;
constexpr float kCardPadding = 22;
constexpr float kCardGap = 18;
constexpr float kAccentWidth = 5;
constexpr float kBlockGap = 10;
constexpr float kVerticalBlockHeight = 330;
// The HTML panel: its padding around the text, and the gap below it.
constexpr float kSourcePaddingX = 8;
constexpr float kSourcePaddingY = 6;
constexpr float kSourceGap = 18;
constexpr ColorARGB kSourceColor = 0xfff6f7f9;
constexpr ColorARGB kSelectionColor = 0xffc9ddfa;
constexpr ColorARGB kPageColor = 0xfff3efe7;
constexpr ColorARGB kCardColor = 0xffffffff;

CSSLength Px(double value) {
  return {value, Unit::kPixels};
}
CSSLength Em(double value) {
  return {value, Unit::kEms};
}
CSSLength Percent(double value) {
  return {value, Unit::kPercentage};
}
StyleColorValue Rgb(uint32_t rgb, int alpha = 255) {
  return StyleColorValue(Color::FromRGBA((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, alpha));
}

// The CSS text of the typed inputs, for the HTML each card shows. Strings are
// single-quoted so that the text can sit in a double-quoted style attribute.
std::string CssNumber(double value) {
  char buffer[32];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  return std::string(buffer, result.ptr);
}
std::string_view CssUnit(Unit unit) {
  switch (unit) {
  case Unit::kPercentage: return "%";
  case Unit::kEms: return "em";
  case Unit::kExs: return "ex";
  case Unit::kPixels: return "px";
  case Unit::kCentimeters: return "cm";
  case Unit::kMillimeters: return "mm";
  case Unit::kInches: return "in";
  case Unit::kPoints: return "pt";
  case Unit::kPicas: return "pc";
  case Unit::kQuarterMillimeters: return "q";
  case Unit::kRems: return "rem";
  case Unit::kRexs: return "rex";
  case Unit::kRchs: return "rch";
  case Unit::kRics: return "ric";
  case Unit::kChs: return "ch";
  case Unit::kIcs: return "ic";
  case Unit::kLhs: return "lh";
  case Unit::kRlhs: return "rlh";
  case Unit::kCaps: return "cap";
  case Unit::kRcaps: return "rcap";
  case Unit::kDegrees: return "deg";
  case Unit::kRadians: return "rad";
  case Unit::kGradians: return "grad";
  case Unit::kTurns: return "turn";
  case Unit::kUnknown:
  case Unit::kNumber:
  case Unit::kInteger: return "";
  }
  return "";
}
std::string CssString(std::string_view utf8) {
  std::string result = "'";
  for (const char c : utf8) {
    if (c == '\'' || c == '\\') result += '\\';
    result += c;
  }
  return result + "'";
}
std::string Join(std::span<const std::string> parts, std::string_view separator) {
  std::string result;
  for (const std::string& part : parts) {
    if (!result.empty() && !part.empty()) result += separator;
    result += part;
  }
  return result;
}

std::string CssText(CSSValueID);
std::string CssText(std::monostate);
std::string CssText(const CSSNumber&);
std::string CssText(const CSSInteger&);
std::string CssText(const CSSLength&);
std::string CssText(const CSSCalc&);
std::string CssText(const String&);
std::string CssText(const AtomicString&);
std::string CssText(const StyleColorValue&);
std::string CssText(const CSSFontFamilyName&);
std::string CssText(const Vector<CSSFontFamilyName>&);
std::string CssText(const CSSFontStyleOblique&);
std::string CssText(const L::FontFeatureSettings::Feature&);
std::string CssText(const Vector<L::FontFeatureSettings::Feature>&);
std::string CssText(const std::unique_ptr<L::FontPalette::Mix>&);
std::string CssText(const L::FontSizeAdjust::WithMetric&);
std::string CssText(const L::FontVariantAlternates::Alternates&);
std::string CssText(const L::FontVariationSettings::Axis&);
std::string CssText(const Vector<L::FontVariationSettings::Axis>&);
std::string CssText(const L::TextEmphasisPosition::Input&);
std::string CssText(const L::TextEmphasisStyle::FillAndShape&);
std::string CssText(const L::TextShadow::Shadow&);
std::string CssText(const Vector<L::TextShadow::Shadow>&);
std::string CssText(const L::TextUnderlinePosition::Parts&);
std::string CssText(const S::WhiteSpace::Longhands&);
template <typename... Types>
std::string CssText(const std::variant<Types...>&);
template <typename T>
std::string CssText(const Vector<T>&);

// A space-separated list; the comma-separated lists have their overloads.
template <typename T>
std::string CssText(const Vector<T>& values) {
  std::vector<std::string> parts;
  for (const T& value : values) parts.push_back(CssText(value));
  return Join(parts, " ");
}
template <typename T>
std::string CssCommaList(const Vector<T>& values) {
  std::vector<std::string> parts;
  for (const T& value : values) parts.push_back(CssText(value));
  return Join(parts, ", ");
}
template <typename... Types>
std::string CssText(const std::variant<Types...>& value) {
  return std::visit([](const auto& alternative) { return CssText(alternative); }, value);
}

std::string CssText(CSSValueID id) {
  return std::string(GetCSSValueName(id));
}
std::string CssText(std::monostate) {
  return "";
}
std::string CssText(const CSSNumber& number) {
  return CssNumber(number.value);
}
std::string CssText(const CSSInteger& integer) {
  return std::to_string(integer.value);
}
std::string CssText(const CSSLength& length) {
  return CssNumber(length.value) + std::string(CssUnit(length.unit));
}
std::string CssText(const CSSCalc& calc) {
  std::string result = "calc(";
  for (const auto& term : calc.terms) {
    if (&term != calc.terms.data()) result += term.value < 0 ? " - " : " + ";
    const double value = &term != calc.terms.data() ? std::abs(term.value) : term.value;
    result += CssNumber(value) + std::string(CssUnit(term.unit));
  }
  return result + ")";
}
std::string CssText(const String& string) {
  return CssString(string.Utf8());
}
std::string CssText(const AtomicString& ident) {
  return ident.Utf8();
}
std::string CssText(const StyleColorValue& color) {
  return color.IsCurrentColor() ? "currentcolor" : color.GetColor().SerializeAsCSSColor().Utf8();
}
std::string CssText(const CSSFontFamilyName& family) {
  return family.generic != CSSValueID::kInvalid ? CssText(family.generic) : CssString(family.name.Utf8());
}
std::string CssText(const Vector<CSSFontFamilyName>& families) {
  return CssCommaList(families);
}
std::string CssText(const CSSFontStyleOblique& oblique) {
  return "oblique " + CssText(oblique.angle);
}
std::string CssText(const L::FontFeatureSettings::Feature& feature) {
  const std::string value = CssText(feature.value);
  return CssString(feature.tag.Utf8()) + (value.empty() ? "" : " " + value);
}
std::string CssText(const Vector<L::FontFeatureSettings::Feature>& features) {
  return CssCommaList(features);
}
std::string CssText(const std::unique_ptr<L::FontPalette::Mix>& mix) {
  std::string result = "palette-mix(";
  if (mix->color_space != Color::ColorSpace::kNone) {
    result += "in " + Color::SerializeInterpolationSpace(mix->color_space, mix->hue_interpolation).Utf8() + ", ";
  }
  result += CssText(mix->palette1);
  if (mix->percentage1) result += " " + CssText(*mix->percentage1);
  result += ", " + CssText(mix->palette2);
  if (mix->percentage2) result += " " + CssText(*mix->percentage2);
  return result + ")";
}
std::string CssText(const L::FontSizeAdjust::WithMetric& adjust) {
  return CssText(adjust.metric) + " " + CssText(adjust.value);
}
std::string CssText(const L::FontVariantAlternates::Alternates& alternates) {
  std::vector<std::string> parts;
  const auto function = [&](const char* name, std::span<const AtomicString> idents) {
    std::vector<std::string> arguments;
    for (const AtomicString& ident : idents) arguments.push_back(CssText(ident));
    if (!arguments.empty()) parts.push_back(std::string(name) + "(" + Join(arguments, ", ") + ")");
  };
  const auto optional = [&](const char* name, const std::optional<AtomicString>& ident) {
    if (ident) function(name, std::span<const AtomicString>(&*ident, 1));
  };
  optional("stylistic", alternates.stylistic);
  if (alternates.historical_forms) parts.push_back("historical-forms");
  function("styleset", {alternates.styleset.data(), alternates.styleset.size()});
  function("character-variant", {alternates.character_variant.data(), alternates.character_variant.size()});
  optional("swash", alternates.swash);
  optional("ornaments", alternates.ornaments);
  optional("annotation", alternates.annotation);
  return Join(parts, " ");
}
std::string CssText(const L::FontVariationSettings::Axis& axis) {
  return CssString(axis.tag.Utf8()) + " " + CssText(axis.value);
}
std::string CssText(const Vector<L::FontVariationSettings::Axis>& axes) {
  return CssCommaList(axes);
}
std::string CssText(const L::TextEmphasisPosition::Input& position) {
  return position.left_right == CSSValueID::kInvalid ? CssText(position.over_under)
                                                     : CssText(position.over_under) + " " + CssText(position.left_right);
}
std::string CssText(const L::TextEmphasisStyle::FillAndShape& style) {
  return CssText(style.fill) + " " + CssText(style.shape);
}
std::string CssText(const L::TextShadow::Shadow& shadow) {
  std::string result = CssText(shadow.x) + " " + CssText(shadow.y);
  if (shadow.blur) result += " " + CssText(*shadow.blur);
  if (shadow.color) result += " " + CssText(*shadow.color);
  return result;
}
std::string CssText(const Vector<L::TextShadow::Shadow>& shadows) {
  return CssCommaList(shadows);
}
std::string CssText(const L::TextUnderlinePosition::Parts& parts) {
  std::vector<std::string> keywords;
  if (parts.position != CSSValueID::kInvalid) keywords.push_back(CssText(parts.position));
  if (parts.side != CSSValueID::kInvalid) keywords.push_back(CssText(parts.side));
  return Join(keywords, " ");
}
std::string CssText(const S::WhiteSpace::Longhands& longhands) {
  std::vector<std::string> keywords;
  if (longhands.collapse) keywords.push_back(CssText(*longhands.collapse));
  if (longhands.wrap_mode) keywords.push_back(CssText(*longhands.wrap_mode));
  return Join(keywords, " ");
}

// HTML text and attribute escaping. Soft hyphens are spelled out, as they are
// invisible in the source.
std::string HtmlText(std::string_view utf8) {
  std::string result;
  for (std::size_t i = 0; i < utf8.size(); ++i) {
    const char c = utf8[i];
    if (c == '&') result += "&amp;";
    else if (c == '<') result += "&lt;";
    else if (c == '>') result += "&gt;";
    else if (utf8.substr(i, 2) == "\xc2\xad") {
      result += "&shy;";
      ++i;
    } else result += c;
  }
  return result;
}
std::string HtmlAttribute(std::string_view name, std::string_view value) {
  if (value.empty()) return "";
  std::string result = " " + std::string(name) + "=\"";
  for (const char c : value) {
    if (c == '&') result += "&amp;";
    else if (c == '"') result += "&quot;";
    else result += c;
  }
  return result + "\"";
}

// The page's downloaded fonts: family names mapped to font files, as
// @font-face rules with a single binary source would give them, in place of
// CSSFontSelector and its FontFaceCache. Other families, generic families and
// character fallback go to the system font cache as without a selector.
class FileFontSelector final : public FontSelector {
public:
  // Loads `path` like a web font (FontCustomPlatformData::Create(): OTS
  // sanitizing, WOFF/WOFF2 decoding) as `family`. Reports and returns false
  // when the file is missing or rejected.
  bool AddFontFile(const AtomicString& family, const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
      std::fprintf(stderr, "font file not found: %s\n", path);
      return false;
    }
    // One allocation of the file's size, instead of growing byte by byte.
    file.seekg(0, std::ios::end);
    Vector<uint8_t> data(static_cast<wtf_size_t>(file.tellg()));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(data.data()), data.size())) {
      std::fprintf(stderr, "font file not readable: %s\n", path);
      return false;
    }
    String message;
    std::shared_ptr<FontCustomPlatformData> face = FontCustomPlatformData::Create(data, message);
    if (!face) {
      std::fprintf(stderr, "font file rejected: %s (%s)\n", path, message.Utf8().c_str());
      return false;
    }
    faces_.Set(family, std::move(face));
    return true;
  }

  std::shared_ptr<const FontData> GetFontData(const FontDescription& description, const FontFamily& family) override {
    if (family.FamilyIsGeneric()) return nullptr;
    const auto face = faces_.find(family.FamilyName());
    if (face == faces_.end()) return nullptr;
    // A face with the default descriptors: normal width, slope and weight.
    const FontSelectionCapabilities capabilities{FontSelectionRange(kNormalWidthValue),
                                                 FontSelectionRange(kNormalSlopeValue),
                                                 FontSelectionRange(kNormalWeightValue)};
    // CSSSegmentedFontFace::GetFontData(): bold and italic requests the face
    // cannot meet are synthesized.
    FontDescription requested(description);
    const FontSelectionRequest request = description.GetFontSelectionRequest();
    requested.SetSyntheticBold(capabilities.weight.maximum < kBoldThreshold && request.weight >= kBoldThreshold &&
                               description.SyntheticBoldAllowed());
    requested.SetSyntheticItalic(capabilities.slope.maximum < kItalicSlopeValue && request.slope >= kItalicSlopeValue &&
                                 description.SyntheticItalicAllowed());
    // BinaryDataFontFaceSource::CreateFontData().
    return std::make_shared<SimpleFontData>(
        face->value->GetFontPlatformData(
            requested.EffectiveFontSize(), requested.AdjustedSpecifiedSize(),
            requested.IsSyntheticBold() && requested.SyntheticBoldAllowed(),
            requested.IsSyntheticItalic() && requested.SyntheticItalicAllowed(), request, capabilities,
            requested.FontOpticalSizing(), requested.TextRendering(), requested.ResolveFontFeatures(),
            requested.Orientation(), requested.VariationSettings(), requested.GetFontPalette()),
        std::make_shared<CustomFontData>());
  }
  bool IsPlatformFamilyMatchAvailable(const FontDescription& description, const FontFamily& family) override {
    return FontCache::Get().IsPlatformFamilyMatchAvailable(description, family.FamilyName());
  }
  // The faces never change, so there is nothing to report or invalidate.
  void WillUseFontData(const FontDescription&, const FontFamily&, const String&) override {}
  void WillUseRange(const FontDescription&, const AtomicString&, const FontDataForRangeSet&) override {}
  unsigned Version() const override {
    return 0;
  }
  void ReportSuccessfulFontFamilyMatch(const AtomicString&) override {}
  void ReportFailedFontFamilyMatch(const AtomicString&) override {}
  void ReportSuccessfulLocalFontMatch(const AtomicString&) override {}
  void ReportFailedLocalFontMatch(const AtomicString&) override {}
  void ReportNotDefGlyph() const override {}
  void ReportEmojiSegmentGlyphCoverage(unsigned, unsigned) override {}
  void RegisterForInvalidationCallbacks(FontSelectorClient*) override {}
  void UnregisterForInvalidationCallbacks(FontSelectorClient*) override {}
  ExecutionContext* GetExecutionContext() const override {
    return nullptr;
  }
  FontFaceCache* GetFontFaceCache() override {
    return nullptr;
  }
  void FontCacheInvalidated() override {}

private:
  HashMap<AtomicString, std::shared_ptr<FontCustomPlatformData>> faces_;
};

// Chains typed StyleDeclaration setters. A rejected value is reported and
// skipped, as a parser would drop an invalid declaration. Alongside, keeps
// the CSS text of each accepted declaration for the card's HTML.
class Css {
public:
  // Builds the property's value with its Make() (style/css_properties.h).
  template <typename Property>
  Css& Set(const typename Property::Input& input) {
    if (Check(declaration_.Set<Property>(input), Property::kId)) {
      const CSSPropertyMetadata* metadata = GetCSSPropertyMetadata(Property::kId);
      SetText(std::string(metadata->name), CssText(input));
    }
    return *this;
  }
  Css& FontFamily(std::initializer_list<const char*> names, V generic) {
    Vector<CSSFontFamilyName> families;
    for (const char* name : names) families.push_back(CSSFontFamilyName{CSSValueID::kInvalid, AtomicString(String::FromUTF8(name))});
    families.push_back(CSSFontFamilyName{generic, AtomicString()});
    return Set<L::FontFamily>(families);
  }
  Css& TextColor(StyleColorValue color) {
    return Set<L::Color>(color);
  }
  Css& FillColor(StyleColorValue color) {
    return Set<L::WebkitTextFillColor>(color);
  }
  Css& StrokeColor(StyleColorValue color) {
    return Set<L::WebkitTextStrokeColor>(color);
  }
  Css& DecorationColor(StyleColorValue color) {
    return Set<L::TextDecorationColor>(color);
  }
  Css& EmphasisColor(StyleColorValue color) {
    return Set<L::TextEmphasisColor>(color);
  }
  Css& LineHeight(double number) {
    return Set<L::LineHeight>(CSSNumber{number});
  }
  Css& Shadow(std::initializer_list<L::TextShadow::Shadow> shadows) {
    return Set<L::TextShadow>(Vector<L::TextShadow::Shadow>(shadows));
  }
  Css& Features(std::initializer_list<std::pair<const char*, int>> tags) {
    Vector<L::FontFeatureSettings::Feature> features;
    for (const auto& [tag, value] : tags) features.push_back(L::FontFeatureSettings::Feature{AtomicString(tag), CSSInteger{value}});
    return Set<L::FontFeatureSettings>(features);
  }
  Css& Variations(std::initializer_list<std::pair<const char*, double>> axes) {
    Vector<L::FontVariationSettings::Axis> variations;
    for (const auto& [tag, value] : axes) variations.push_back(L::FontVariationSettings::Axis{AtomicString(tag), CSSNumber{value}});
    return Set<L::FontVariationSettings>(variations);
  }
  // -webkit-locale, which HTML gives with the lang attribute.
  Css& Locale(const char* tag) {
    if (Check(declaration_.Set<L::WebkitLocale>(String(tag)), L::WebkitLocale::kId)) lang_ = tag;
    return *this;
  }
  Css& Merge(const Css& other) {
    declaration_.Merge(other.declaration_);
    for (const auto& [name, value] : other.text_) SetText(name, value);
    if (!other.lang_.empty()) lang_ = other.lang_;
    return *this;
  }
  const StyleDeclaration& Declaration() const {
    return declaration_;
  }
  // The declarations as a style attribute value; Lang() is the lang attribute.
  std::string Text() const {
    std::vector<std::string> declarations;
    for (const auto& [name, value] : text_) declarations.push_back(name + ": " + value);
    return Join(declarations, "; ");
  }
  const std::string& Lang() const {
    return lang_;
  }

private:
  bool Check(bool ok, P property) {
    if (!ok) {
      const CSSPropertyMetadata* metadata = GetCSSPropertyMetadata(property);
      std::fprintf(stderr, "rejected declaration: %.*s\n", metadata ? static_cast<int>(metadata->name.size()) : 1,
                   metadata ? metadata->name.data() : "?");
    }
    return ok;
  }
  // A rewritten property moves to the end, so the text keeps the meaning of
  // the declaration however shorthands and longhands interleave.
  void SetText(const std::string& name, std::string value) {
    std::erase_if(text_, [&](const auto& entry) { return entry.first == name; });
    text_.emplace_back(name, std::move(value));
  }
  StyleDeclaration declaration_;
  std::vector<std::pair<std::string, std::string>> text_;
  std::string lang_;
};

// The HTML of one block: the root's declarations and attributes, and the
// markup of its children.
struct BlockSource {
  Css root;
  std::string markup;
};

// One paragraph, laid out as its own block container.
struct Block {
  std::unique_ptr<InlineFormattingContext> context;
  // Null for a card's HTML panel.
  std::unique_ptr<BlockSource> source;
  PhysicalOffset offset;
  PhysicalSize size;
};

struct Card {
  ColorARGB accent = 0;
  bool vertical = false;
  // The language label, a comment at the top of the HTML.
  const char* label = "";
  // Rules the HTML needs ahead of the card's, such as @font-face.
  std::string font_faces;
  // The HTML of the blocks, shown in `source` above them. LayoutPage puts the
  // card width in CSS pixels between the head and the tail, at
  // `html_width_offset` of the text `source_text` holds.
  std::string html_head;
  std::string html_tail;
  std::string html_width;
  unsigned html_width_offset = 0;
  Block source;
  const InlineObject* source_text = nullptr;
  Vector<Block> blocks;
  float top = 0;
  float height = 0;
};

// Rules shared by every block. Names replace selectors; a node lists the
// rules it matches, in cascade order, before its own declarations. The HTML
// gives them as class selectors.
struct Rule {
  const char* name;
  Css css;
};

std::vector<Rule> Rules() {
  std::vector<Rule> rules;
  rules.push_back({"title", Css().Set<L::FontWeight>(CSSNumber{700}).Set<L::FontSize>(Em(1.35)).TextColor(Rgb(0x1f2937))});
  rules.push_back({"name", Css().Set<L::FontWeight>(CSSNumber{600}).TextColor(Rgb(0x9a3412))});
  return rules;
}

void InstallRules(InlineFormattingContext& context) {
  for (const Rule& rule : Rules()) (void)context.StyleSheet().SetRule(AtomicString(rule.name), rule.css.Declaration());
}

std::unique_ptr<InlineFormattingContext> NewContext(const Css& root, std::shared_ptr<FontSelector> fonts = nullptr) {
  auto context = std::make_unique<InlineFormattingContext>(Settings(), std::move(fonts));
  InstallRules(*context);
  context->SetInlineStyle(context->RootObject(), root.Declaration());
  return context;
}

// A block being built. Each append also adds its markup to the block's HTML;
// children are appended to the root, so the markup is flat.
struct Paragraph {
  InlineFormattingContext& context;
  BlockSource& source;
};

Paragraph AddBlock(Card& card, const Css& root, std::shared_ptr<FontSelector> fonts = nullptr) {
  card.blocks.push_back(Block{NewContext(root, std::move(fonts)), std::make_unique<BlockSource>(BlockSource{root, {}})});
  const Block& block = card.blocks.back();
  return {*block.context, *block.source};
}

void Text(Paragraph paragraph, const char* utf8) {
  paragraph.context.AppendText(paragraph.context.RootObject(), String::FromUTF8(utf8));
  paragraph.source.markup += HtmlText(utf8);
}

void Span(Paragraph paragraph, const char* utf8, const Css& css, std::initializer_list<const char*> rules = {}) {
  InlineFormattingContext& context = paragraph.context;
  const InlineObject& span = context.AppendInline(context.RootObject());
  std::vector<std::string> classes;
  if (rules.size()) {
    Vector<AtomicString> names;
    for (const char* rule : rules) {
      names.push_back(AtomicString(rule));
      classes.push_back(rule);
    }
    context.SetRules(span, std::move(names));
  }
  context.SetInlineStyle(span, css.Declaration());
  context.AppendText(span, String::FromUTF8(utf8));
  paragraph.source.markup += "<span" + HtmlAttribute("class", Join(classes, " ")) + HtmlAttribute("lang", css.Lang()) +
                             HtmlAttribute("style", css.Text()) + ">" + HtmlText(utf8) + "</span>";
}

void Span(Paragraph paragraph, const char* utf8, std::initializer_list<const char*> rules) {
  Span(paragraph, utf8, Css(), rules);
}

Card NewCard(ColorARGB accent, const char* label) {
  Card card;
  card.accent = accent;
  card.label = label;
  return card;
}

// 简体中文: justification, emphasis marks, CJK/Latin autospace, strict
// line breaking and decorated quotations.
Card ChineseCard() {
  Card card = NewCard(0xffb91c1c, "简体中文 · zh-Hans");
  const Css base = Css()
                       .FontFamily({"Microsoft YaHei", "Noto Sans CJK SC", "PingFang SC"}, V::kSansSerif)
                       .Set<L::FontSize>(Px(17))
                       .LineHeight(1.75)
                       .Locale("zh-Hans")
                       .TextColor(Rgb(0x1f2937))
                       .Set<L::LineBreak>(V::kStrict)
                       .Set<L::TextAutospace>(V::kNormal)
                       .Set<L::TextSpacingTrim>(V::kTrimStart);

  auto title = AddBlock(card, Css().Merge(base).Set<L::TextWrapStyle>(V::kBalance));
  Span(title, "无职转生～到了异世界就拿出真本事～", Css().Set<L::LetterSpacing>(Em(0.06)), {"title"});

  const Css paragraph = Css().Merge(base).Set<L::TextAlign>(V::kJustify).Set<L::TextIndent>(Em(2));
  const Css dots = Css()
                       .Set<L::TextEmphasisStyle>(L::TextEmphasisStyle::FillAndShape{V::kFilled, V::kDot})
                       .Set<L::TextEmphasisPosition>({V::kUnder, V::kRight})
                       .EmphasisColor(Rgb(0xb91c1c));
  auto p1 = AddBlock(card, paragraph);
  Text(p1, "一名34岁、足不出户的无业男子，在被家人赶出家门的那天，为了救下几名高中生而被失控的卡车撞死。"
           "再次睁开眼时，他已成为剑与魔法世界里的婴儿——");
  Span(p1, "鲁迪乌斯·格雷拉特", Css().Merge(dots), {"name"});
  Text(p1, "。这部Web小说自2012年起连载于「成为小说家吧」，后来被改编为轻小说与TV动画。");

  auto p2 = AddBlock(card, paragraph);
  Text(p2, "带着前世的记忆，他从三岁起跟随家庭教师");
  Span(p2, "洛琪希", Css().Merge(dots), {"name"});
  Text(p2, "学习魔术，与青梅竹马希露菲一同长大；转移事件又将他与大小姐艾莉丝抛到魔大陆的尽头。他在心中立誓：");
  Span(p2, "「这一次，我要认真地活下去。」",
       Css()
           .Set<L::TextDecorationLine>(Vector<V>{V::kUnderline})
           .Set<L::TextDecorationStyle>(V::kWavy)
           .DecorationColor(Rgb(0xdc2626))
           .Set<L::TextDecorationThickness>(Px(1.5))
           .Set<L::TextUnderlinePosition>(L::TextUnderlinePosition::Parts{.position = V::kUnder})
           .Set<L::TextDecorationSkipInk>(V::kAuto)
           .Set<L::FontWeight>(CSSNumber{700}));
  return card;
}

// 日本語: vertical-rl with tate-chū-yoko, sesame emphasis and JIS04 forms.
Card JapaneseCard() {
  Card card = NewCard(0xff7c3aed, "日本語 · ja · vertical-rl");
  card.vertical = true;
  auto block = AddBlock(card, Css()
                                  .Set<L::WritingMode>(V::kVerticalRl)
                                  .Set<L::TextOrientation>(V::kMixed)
                                  .FontFamily({"Yu Mincho", "游明朝", "Noto Serif CJK JP", "MS Mincho"}, V::kSerif)
                                  .Set<L::FontSize>(Px(19))
                                  .LineHeight(1.85)
                                  .Locale("ja")
                                  .TextColor(Rgb(0x111827))
                                  .Set<L::LineBreak>(V::kStrict)
                                  .Set<L::FontVariantEastAsian>(Vector<V>{V::kJis04})
                                  .Set<L::WebkitFontSmoothing>(V::kAntialiased)
                                  .Set<S::WhiteSpace>(V::kPreLine));
  const Css combine = Css().Set<L::TextCombineUpright>(V::kAll);
  Span(block, "無職転生　〜異世界行ったら本気だす〜",
       Css().Features({{"vpal", 1}}).TextColor(Rgb(0x5b21b6)), {"title"});
  Text(block, "\n");
  Span(block, "34", combine);
  Text(block, "歳・無職・引きこもりの男は、家を追い出されたその日、トラックから高校生を庇って命を落とした。"
              "目を覚ますと、そこは剣と魔法の異世界。彼は");
  Span(block, "ルーデウス・グレイラット", {"name"});
  Text(block, "として生まれ変わり、前世の記憶を抱えたまま、今度こそ");
  Span(block, "本気",
       Css().Set<L::TextEmphasisStyle>(V::kSesame).EmphasisColor(Rgb(0xdb2777)));
  Text(block, "で生きると誓う。\n家庭教師ロキシーに魔術を学び、幼なじみのシルフィと育ち、"
              "転移事件で魔大陸へ飛ばされたエリスと共に、");
  Span(block, "3", combine);
  Text(block, "年に及ぶ帰郷の旅へ――。");
  return card;
}

// 한국어: keep-all keeps Hangul words whole; the title is balanced.
Card KoreanCard() {
  Card card = NewCard(0xff0369a1, "한국어 · ko");
  const Css base = Css()
                       .FontFamily({"Malgun Gothic", "맑은 고딕", "Noto Sans CJK KR"}, V::kSansSerif)
                       .Set<L::FontSize>(Px(17))
                       .LineHeight(1.7)
                       .Locale("ko")
                       .TextColor(Rgb(0x1e293b))
                       .Set<L::WordBreak>(V::kKeepAll)
                       .Set<L::TextWrapStyle>(V::kBalance)
                       .Set<L::TextAlign>(V::kCenter);
  auto title = AddBlock(card, base);
  Span(title, "무직전생 ~이세계에 갔으면 최선을 다한다~",
       Css().Shadow({{Px(1), Px(2), Px(3), Rgb(0x0369a1, 90)}}), {"title"});

  auto p = AddBlock(card, base);
  Text(p, "서른네 살의 백수 은둔형 외톨이가 집에서 쫓겨난 날, 트럭에 치일 뻔한 학생들을 구하고 목숨을 잃는다. "
          "다시 눈을 뜬 곳은 검과 마법의 세계. 그는 ");
  Span(p, "루데우스 그레이랫", {"name"});
  Text(p, "이라는 이름의 아기로 다시 태어난다. ");
  Span(p, "전생의 기억을 간직한 채, 이번 인생만큼은 후회 없이 진심으로 살아가겠다고 다짐한다.",
       Css().Set<L::FontStyle>(CSSFontStyleOblique{CSSLength{12, Unit::kDegrees}}).Set<L::FontWeight>(CSSNumber{300}));
  return card;
}

// English: hyphens, ligatures, small caps, outlined and shadowed text, a
// variable font, a superscript and an unbreakable URL.
Card EnglishCard() {
  Card card = NewCard(0xff15803d, "English · en");
  const Css base = Css()
                       .FontFamily({"Georgia", "Times New Roman"}, V::kSerif)
                       .Set<L::FontSize>(Px(17))
                       .LineHeight(1.6)
                       .Locale("en")
                       .TextColor(Rgb(0x1c1917))
                       .Set<L::FontKerning>(V::kNormal)
                       .Set<L::TextRendering>(V::kOptimizelegibility)
                       .Set<L::FontVariantLigatures>(Vector<V>{V::kCommonLigatures, V::kDiscretionaryLigatures})
                       .Set<L::FontVariantNumeric>(Vector<V>{V::kOldstyleNums, V::kProportionalNums});

  auto title = AddBlock(card, base);
  Span(title, "Mushoku Tensei",
       Css().Set<L::TextTransform>(V::kUppercase).Set<L::LetterSpacing>(Em(0.12)).TextColor(Rgb(0x14532d)), {"title"});
  Text(title, "  ");
  Span(title, "Jobless Reincarnation",
       Css().Set<L::FontStyle>(V::kItalic).Set<L::FontSize>(Px(21)).TextColor(Rgb(0x57534e)));

  const Css paragraph = Css()
                            .Merge(base)
                            .Set<L::TextAlign>(V::kJustify)
                            .Set<L::TextAlignLast>(V::kLeft)
                            .Set<L::Hyphens>(V::kManual)
                            .Set<L::HyphenateCharacter>(String::FromUTF8("‐"))
                            .Set<L::WordSpacing>(Px(1))
                            .Set<L::TextWrapStyle>(V::kPretty)
                            .Set<L::OverflowWrap>(V::kAnywhere);
  auto p1 = AddBlock(card, paragraph);
  Text(p1, "A thirty-four-year-old shut-in, thrown out of his family’s house, dies pushing a group of "
           "students out of the path of a speeding truck. He wakes as a baby in a world of swords and sorcery: ");
  Span(p1, "Rudeus Greyrat", Css().Set<L::FontVariantCaps>(V::kSmallCaps).Set<L::LetterSpacing>(Em(0.03)),
       {"name"});
  Text(p1, ", son of a swordsman and a healer. Determined not to waste his second life, he masters magic "
           "under the e­nig­mat­ic tutor ");
  Span(p1, "Roxy Migurdia",
       Css()
           .FillColor(Rgb(0xffffff, 0))
           .StrokeColor(Rgb(0x1d4ed8))
           .Set<L::WebkitTextStrokeWidth>(Px(0.8))
           .Set<L::FontWeight>(CSSNumber{700}));
  Text(p1, ", grows up beside his childhood friend Sylphiette, and is flung across the world with the fiery "
           "noble ");
  Span(p1, "Eris Boreas Greyrat",
       Css().TextColor(Rgb(0xb91c1c)).Shadow({{Px(0), Px(0), Px(6), Rgb(0xf97316, 160)}, {Px(1), Px(1), {}, {}}}));
  Text(p1, " by the Teleport Incident");
  Span(p1, "1", Css().Set<L::VerticalAlign>(V::kSuper).Set<L::FontSize>(Percent(70)));
  Text(p1, ". Its first official, fluent translation sold dis­pro­por­tion­ate­ly well. Source: ");
  Span(p1, "https://ncode.syosetu.com/n9669bk/", Css().FontFamily({"Cascadia Mono", "Consolas"}, V::kMonospace)
                                                      .Set<L::FontSize>(Px(14))
                                                      .TextColor(Rgb(0x2563eb))
                                                      .Set<L::TextDecorationLine>(Vector<V>{V::kUnderline})
                                                      .Set<L::TextUnderlineOffset>(Px(3)));

  auto p2 = AddBlock(card, Css().Merge(base).Set<L::TextAlign>(V::kCenter));
  Span(p2, "“This time, I’ll live my life to the fullest.”",
       Css()
           .Set<L::FontStyle>(V::kItalic)
           .Set<L::FontSize>(Px(20))
           .Set<L::TextDecorationLine>(Vector<V>{V::kUnderline, V::kOverline})
           .Set<L::TextDecorationStyle>(V::kDouble)
           .DecorationColor(Rgb(0x16a34a)));
  Text(p2, "  ");
  Span(p2, "Light novel vol. 1–26 · 2014–2022",
       Css()
           .FontFamily({"Bahnschrift"}, V::kSansSerif)
           .Set<L::FontStretch>(Percent(75))
           .Variations({{"wght", 600}})
           .Set<L::FontVariantNumeric>(Vector<V>{V::kLiningNums, V::kTabularNums})
           .Set<L::FontSize>(Px(15))
           .TextColor(Rgb(0x4d7c0f)));

  auto note = AddBlock(card, Css().Merge(base).Set<L::FontSize>(Px(13)).TextColor(Rgb(0x78716c)));
  Text(note, "1. Spoiler: ");
  Span(note, "Fittoa becomes a grassland", Css().Set<L::Visibility>(V::kHidden));
  Text(note, " — hidden with visibility: hidden, which still takes up space.");
  return card;
}

// ไทย: dictionary-based word breaking without spaces, justified.
Card ThaiCard() {
  Card card = NewCard(0xffca8a04, "ภาษาไทย · th");
  const Css base = Css()
                       .FontFamily({"Leelawadee UI", "Tahoma", "Noto Sans Thai"}, V::kSansSerif)
                       .Set<L::FontSize>(Px(18))
                       .LineHeight(1.9)
                       .Locale("th")
                       .TextColor(Rgb(0x292524));
  auto title = AddBlock(card, base);
  Span(title, "เกิดชาตินี้พี่ต้องเทพ", Css().TextColor(Rgb(0x854d0e)), {"title"});

  auto p = AddBlock(card, Css().Merge(base).Set<L::TextAlign>(V::kJustify));
  Text(p, "ชายวัยสามสิบสี่ปีผู้เก็บตัวอยู่แต่ในห้องถูกครอบครัวไล่ออกจากบ้าน "
          "และเสียชีวิตขณะช่วยนักเรียนให้พ้นจากรถบรรทุก เขาลืมตาขึ้นอีกครั้งในฐานะทารกชื่อ ");
  Span(p, "รูเดียส เกรย์แรต", {"name"});
  Text(p, " ในโลกแห่งดาบและเวทมนตร์ จาก");
  Span(p, "คนตกงาน", Css()
                         .Set<L::TextDecorationLine>(Vector<V>{V::kLineThrough})
                         .DecorationColor(Rgb(0xdc2626))
                         .Set<L::TextDecorationThickness>(Px(2))
                         .TextColor(Rgb(0x78716c)));
  Text(p, " สู่จอมเวทผู้ยิ่งใหญ่ ด้วยความทรงจำจากชาติก่อน "
          "เขาตั้งปณิธานว่าชาตินี้จะใช้ชีวิตอย่างจริงจังโดยไม่ต้องเสียใจภายหลัง");
  return card;
}

// العربية: right-to-left base direction with isolated Latin and numbers.
Card ArabicCard() {
  Card card = NewCard(0xff0f766e, "العربية · ar · rtl");
  const Css base = Css()
                       .Set<L::Direction>(V::kRtl)
                       .FontFamily({"Segoe UI", "Arial", "Noto Naskh Arabic"}, V::kSansSerif)
                       .Set<L::FontSize>(Px(19))
                       .LineHeight(1.85)
                       .Locale("ar")
                       .TextColor(Rgb(0x1f2937));
  auto title = AddBlock(card, base);
  Span(title, "مشوكو تنساي: تناسخ العاطل عن العمل", Css().TextColor(Rgb(0x115e59)), {"title"});

  const Css ltr = Css().Set<L::Direction>(V::kLtr).Set<L::UnicodeBidi>(V::kIsolate);
  auto p = AddBlock(card, base);
  Text(p, "رجلٌ عاطلٌ عن العمل في الرابعة والثلاثين (");
  Span(p, "34", Css().Merge(ltr).Set<L::FontSizeAdjust>(CSSNumber{0.55}));
  Text(p, ") طردته عائلته من البيت، فلقي حتفه وهو ينقذ طلابًا من شاحنة مسرعة. "
          "ثم فتح عينيه رضيعًا في عالمٍ من السيوف والسحر باسم ");
  Span(p, "روديوس غريرات", {"name"});
  Text(p, " (");
  Span(p, "Rudeus Greyrat", Css().Merge(ltr).Set<L::FontStyle>(V::kItalic));
  Text(p, "). وبذكريات حياته السابقة تعلّم السحر على يد معلّمته روكسي، ");
  Span(p, "وأقسم أن يعيش هذه المرة بجدّيةٍ ومن دون ندم.",
       Css()
           .Set<L::TextDecorationLine>(Vector<V>{V::kUnderline})
           .Set<L::TextDecorationStyle>(V::kDotted)
           .DecorationColor(Rgb(0x0f766e))
           .Set<L::TextUnderlineOffset>(Px(5)));
  return card;
}

// More scripts, one block each: Cyrillic, Devanagari, Vietnamese stacked
// diacritics and Hebrew.
Card MoreScriptsCard() {
  Card card = NewCard(0xff9333ea, "Русский · हिन्दी · Tiếng Việt · עברית");
  const Css base = Css().Set<L::FontSize>(Px(17)).LineHeight(1.65).TextColor(Rgb(0x1f2937));

  auto ru = AddBlock(card, Css().Merge(base).FontFamily({"Segoe UI"}, V::kSansSerif).Locale("ru"));
  Span(ru, "реинкарнация безработного", Css().Set<L::TextTransform>(V::kCapitalize), {"name"});
  Text(ru, ": тридцатичетырёхлетний затворник перерождается в мире меча и магии под именем ");
  Span(ru, "Рудеус Грейрат",
       Css().Set<L::FontVariantCaps>(V::kAllSmallCaps).Set<L::FontSynthesisSmallCaps>(V::kAuto));
  Text(ru, ".");

  auto hi = AddBlock(card, Css().Merge(base).FontFamily({"Nirmala UI", "Noto Sans Devanagari"}, V::kSansSerif)
                                .Locale("hi"));
  Text(hi, "बेरोज़गार पुनर्जन्म: चौंतीस वर्ष का एक बेरोज़गार व्यक्ति तलवार और जादू की दुनिया में ");
  Span(hi, "रूडियस ग्रेरैट", {"name"});
  Text(hi, " के रूप में फिर से जन्म लेता है।");

  auto vi = AddBlock(card, Css().Merge(base).FontFamily({"Segoe UI", "Arial"}, V::kSansSerif).Locale("vi"));
  Text(vi, "Thất nghiệp chuyển sinh");
  Span(vi, "WN",
       Css().Set<L::FontVariantPosition>(V::kSuper).Set<L::FontSynthesisWeight>(V::kNone));
  Text(vi, ": một gã thất nghiệp ba mươi tư tuổi được tái sinh thành ");
  Span(vi, "Rudeus Greyrat", Css().Set<L::LetterSpacing>(Px(1.5)), {"name"});
  Text(vi, " trong thế giới của kiếm và phép thuật.");

  auto he = AddBlock(card, Css()
                               .Merge(base)
                               .FontFamily({"Segoe UI", "Arial"}, V::kSansSerif)
                               .Locale("he")
                               .Set<L::UnicodeBidi>(V::kPlaintext));
  Text(he, "גלגול נשמות של מובטל: גבר מובטל בן 34 נולד מחדש בעולם של חרבות וקסמים בתור ");
  Span(he, "רודאוס גרייראט", {"name"});
  Text(he, " (Rudeus Greyrat).");
  return card;
}

// Preserved tabs and newlines, no wrapping, tabular figures and emoji.
Card TimelineCard() {
  Card card = NewCard(0xff475569, "Timeline · 年表");
  auto block = AddBlock(card, Css()
                                  .FontFamily({"Cascadia Mono", "Consolas"}, V::kMonospace)
                                  .Set<L::FontSize>(Px(14))
                                  .LineHeight(1.7)
                                  .TextColor(Rgb(0x334155))
                                  .Set<S::WhiteSpace>(V::kPre)
                                  .Set<L::TabSize>(CSSNumber{6})
                                  .Set<L::TextWrapMode>(V::kNowrap)
                                  .Set<L::FontVariantNumeric>(Vector<V>{V::kTabularNums, V::kSlashedZero})
                                  .Set<L::FontVariantAlternates>(L::FontVariantAlternates::Alternates{.historical_forms = true}));
  Span(block, "Age\tArc\n", Css().Set<L::FontWeight>(CSSNumber{700}));
  Text(block, "0\tInfancy · 幼年期 · 유년기\n"
              "7\tChildhood · 少年期 · พบกับรอกซี่\n"
              "10\tTeleport Incident · 転移事件 · حادثة الانتقال\n"
              "15\tYouth · 青少年期 · Юность\n");
  Span(block, "⚔︎ ✨ \U0001F4D6 \U0001F9D9‍♀️ \U0001F3E0",
       Css().Set<L::FontVariantEmoji>(V::kEmoji).Set<L::FontPalette>(V::kNormal));
  return card;
}

// Ancient Egyptian hieroglyphs in a downloaded font (Noto Sans Egyptian
// Hieroglyphs, fetched by CMake), loaded through a FontSelector as an
// @font-face rule would load it. The names are spelled with uniliteral signs
// and a determinative; the cartouche frames the hero's name like a royal one.
Card EgyptianCard(const std::shared_ptr<FontSelector>& fonts) {
  Card card = NewCard(0xffb45309, "Ancient Egyptian · 古埃及文 · egy");
  // The @font-face rule the selector stands for. local() finds an installed
  // copy; browsers that refuse fonts from file: URLs need the file served.
  std::string source = "local('Noto Sans Egyptian Hieroglyphs')";
#ifdef BKIT_EXAMPLE_FONT_DIR
  const std::string directory = BKIT_EXAMPLE_FONT_DIR;
  source += ", url(" +
            CssString((directory.starts_with('/') ? "file://" : "file:///") + directory +
                      "/NotoSansEgyptianHieroglyphs-Regular.ttf") +
            ")";
#endif
  card.font_faces = "@font-face { font-family: 'Noto Sans Egyptian Hieroglyphs'; src: " + source + "; }\n";
  // The downloaded font first; Segoe UI Historic also has the hieroglyphs on
  // Windows, the others take the Latin transliteration and the Chinese.
  const Css base = Css()
                       .FontFamily({"Noto Sans Egyptian Hieroglyphs", "Segoe UI Historic", "Segoe UI", "Microsoft YaHei"},
                                   V::kSansSerif)
                       .Locale("egy")
                       .Set<L::TextAlign>(V::kCenter)
                       .TextColor(Rgb(0x1f2937));

  auto title = AddBlock(card, Css().Merge(base).Set<L::FontSize>(Px(44)).LineHeight(1.4).TextColor(Rgb(0x92400e)),
                        fonts);
  Text(title, "\U00013379\U0001308B\U00013171\U000130A7\U000131CC\U000132F4\U0001337A");
  Span(title, "  \U000132F9\U00013351\U000132F4", Css().TextColor(Rgb(0xb45309)));

  // Each name: the hieroglyphs, the transliteration and the Chinese name;
  // two names a line, the line break kept by white-space-collapse.
  auto names = AddBlock(
      card, Css().Merge(base).Set<L::FontSize>(Px(18)).LineHeight(2).Set<L::WhiteSpaceCollapse>(V::kPreserveBreaks),
      fonts);
  const struct {
    const char* glyphs;
    const char* transliteration;
    const char* name;
  } kNames[] = {
      // r w d y s + man
      {"\U0001308B\U00013171\U000130A7\U000131CC\U000132F4\U00013000", "rwdys", "鲁迪乌斯"},
      // r k s y + woman
      {"\U0001308B\U000133A1\U000132F4\U000131CC\U00013050", "rksy", "洛琪希"},
      // i r y s + woman
      {"\U000131CB\U0001308B\U000131CC\U000132F4\U00013050", "irys", "艾莉丝"},
      // s r f t + woman
      {"\U000132F4\U0001308B\U00013191\U000133CF\U00013050", "srft", "希露菲"},
  };
  for (const auto& entry : kNames) {
    if (&entry != kNames) Text(names, (&entry - kNames) % 2 ? " · " : "\n");
    Span(names, entry.glyphs, Css().Set<L::FontSize>(Px(28)).Set<L::LetterSpacing>(Px(2)).TextColor(Rgb(0x92400e)));
    Text(names, " ");
    Span(names, entry.transliteration, Css().Set<L::FontStyle>(V::kItalic).TextColor(Rgb(0x6b7280)));
    Text(names, " ");
    Span(names, entry.name, {"name"});
  }

  auto note = AddBlock(card, Css().Merge(base).Set<L::FontSize>(Px(14)).LineHeight(1.8).TextColor(Rgb(0x78716c)),
                       fonts);
  // ankh wedja seneb: "life, prosperity, health".
  Span(note, "\U000132F9\U00013351\U000132F4", Css().Set<L::FontSize>(Px(20)).TextColor(Rgb(0xb45309)));
  Text(note, " ");
  Span(note, "ꜥnḫ wḏꜣ snb", Css().Set<L::FontStyle>(V::kItalic));
  Text(note, " — 愿他生命、昌盛、健康：王名之后的祝词，此处献给王名圈中的鲁迪乌斯。");
  return card;
}

// The selector with the downloaded fonts; empty when they are missing, so
// the hieroglyphs fall back to the system fonts.
std::shared_ptr<FontSelector> LoadExampleFonts() {
  auto fonts = std::make_shared<FileFontSelector>();
#ifdef BKIT_EXAMPLE_FONT_DIR
  (void)fonts->AddFontFile(AtomicString("Noto Sans Egyptian Hieroglyphs"),
                           BKIT_EXAMPLE_FONT_DIR "/NotoSansEgyptianHieroglyphs-Regular.ttf");
#endif
  return fonts;
}

// The HTML panel above each card's blocks.
Css SourceStyle() {
  return Css()
      .FontFamily({"Cascadia Mono", "Consolas"}, V::kMonospace)
      .Set<L::FontSize>(Px(12))
      .LineHeight(1.5)
      .TextColor(Rgb(0x374151))
      .Set<L::FontVariantLigatures>(V::kNone)
      .Set<S::WhiteSpace>(V::kPreWrap)
      .Set<L::OverflowWrap>(V::kAnywhere)
      .Set<L::TabSize>(CSSNumber{4});
}

// The HTML of the card's blocks, laid out as LayoutPage places them: stacked
// kBlockGap apart, a vertical block kVerticalBlockHeight tall at the right.
void BuildSource(Card& card) {
  card.html_head = std::string("<!-- ") + card.label + " -->\n<style>\n" + card.font_faces + ".card { width: ";
  std::string tail = "px; }\n.card > div + div { margin-top: " + CssNumber(kBlockGap) + "px; }\n";
  for (const Rule& rule : Rules()) tail += "." + std::string(rule.name) + " { " + rule.css.Text() + "; }\n";
  tail += "</style>\n<div class=\"card\">\n";
  for (const Block& block : card.blocks) {
    const BlockSource& source = *block.source;
    std::vector<std::string> style{source.root.Text()};
    if (card.vertical)
      style.push_back("height: " + CssNumber(kVerticalBlockHeight) + "px; width: fit-content; margin-left: auto");
    tail += "<div" + HtmlAttribute("lang", source.root.Lang()) + HtmlAttribute("style", Join(style, "; ")) + ">" +
            source.markup + "</div>\n";
  }
  card.html_tail = tail + "</div>\n";
  card.source.context = NewContext(SourceStyle());
}

// Puts the card width, in CSS pixels, into the card's HTML. Returns whether
// the text changed.
bool UpdateSource(Card& card, double width) {
  const std::string text = CssNumber(std::round(width * 100) / 100);
  if (card.source_text && text == card.html_width) return false;
  InlineFormattingContext& context = *card.source.context;
  if (card.source_text) {
    // Only the digits change, so layout reshapes only the text around them.
    context.ReplaceText(*card.source_text, card.html_width_offset, static_cast<unsigned>(card.html_width.size()),
                        String::FromUTF8(text));
  } else {
    card.html_width_offset = String::FromUTF8(card.html_head).length();
    card.source_text =
        &context.AppendText(context.RootObject(), String::FromUTF8(card.html_head + text + card.html_tail));
  }
  card.html_width = text;
  return true;
}

// A selection in one card's HTML, as offsets in its text.
struct SourceSelection {
  int card = -1;
  unsigned anchor = 0;
  unsigned focus = 0;
  bool operator==(const SourceSelection&) const = default;
};

struct Page {
  std::shared_ptr<FontSelector> fonts = LoadExampleFonts();
  Vector<Card> cards;
  float scale = 0;
  int width = 0;
  float height = 0;
  float scroll = 0;
  bool dirty = true;
  int render_divisor = 1;
  bool linear_filter = false;
  SourceSelection selection;
  bool selecting = false;

  Page() {
    cards.push_back(ChineseCard());
    cards.push_back(JapaneseCard());
    cards.push_back(KoreanCard());
    cards.push_back(EnglishCard());
    cards.push_back(ThaiCard());
    cards.push_back(ArabicCard());
    cards.push_back(MoreScriptsCard());
    cards.push_back(EgyptianCard(fonts));
    cards.push_back(TimelineCard());
    for (Card& card : cards) BuildSource(card);
  }

  void Select(const SourceSelection& next) {
    if (next == selection) return;
    selection = next;
    dirty = true;
  }
};

// Lays out a block for `inline_size` framebuffer pixels and returns its
// physical size.
PhysicalSize LayoutBlock(Block& block, float scale, float inline_size) {
  auto& context = *block.context;
  context.SetZoomFactors(scale);
  auto options = context.Options();
  options.available_inline_size = LayoutUnit(std::floor(inline_size));
  context.SetOptions(options);
  context.UpdateLayout();
  block.size = context.Fragments().SizeInPhysicalCoordinates();
  return block.size;
}

// Positions every card for a framebuffer `width`. Geometry is in framebuffer
// pixels: the zoom factors scale the CSS lengths, not the canvas.
void LayoutPage(Page& page, int width, float scale) {
  if (page.width == width && page.scale == scale) return;
  page.width = width;
  page.scale = scale;
  const float margin = std::round(kPageMargin * scale);
  const float padding = std::round(kCardPadding * scale);
  const float accent = std::round(kAccentWidth * scale);
  const float left = margin + accent + padding;
  const float inner = std::max(1.0f, width - 2 * margin - accent - 2 * padding);
  float y = margin;
  for (Card& card : page.cards) {
    // The blocks are laid out for std::floor(inner) framebuffer pixels.
    if (UpdateSource(card, std::floor(inner) / scale)) page.selection = {};
    card.top = y;
    float cursor = y + padding;
    const PhysicalSize source = LayoutBlock(card.source, scale, inner);
    card.source.offset = PhysicalOffset(LayoutUnit(left), LayoutUnit(cursor));
    cursor += source.height.ToFloat() + std::round(kSourceGap * scale);
    for (Block& block : card.blocks) {
      if (card.vertical) {
        // vertical-rl: the inline size is the height; lines progress leftward,
        // so the block hangs from the right edge of the card.
        const PhysicalSize size = LayoutBlock(block, scale, std::round(kVerticalBlockHeight * scale));
        block.offset = PhysicalOffset(LayoutUnit(std::round(left + inner - size.width.ToFloat())), LayoutUnit(cursor));
        cursor += size.height.ToFloat();
      } else {
        const PhysicalSize size = LayoutBlock(block, scale, inner);
        block.offset = PhysicalOffset(LayoutUnit(left), LayoutUnit(cursor));
        cursor += size.height.ToFloat();
      }
      cursor += std::round(kBlockGap * scale);
    }
    card.height = std::round(cursor - kBlockGap * scale + padding - y);
    y += card.height + std::round(kCardGap * scale);
  }
  page.height = y - std::round(kCardGap * scale) + margin;
}

// The HTML panel of a card, in page coordinates: framebuffer pixels from the
// page top.
ScalarRect SourcePanel(const Card& card, float scale) {
  const float x = std::round(kSourcePaddingX * scale);
  const float y = std::round(kSourcePaddingY * scale);
  const PhysicalRect text(card.source.offset, card.source.size);
  return ScalarRect::MakeXYWH(text.X().ToFloat() - x, text.Y().ToFloat() - y, text.Width().ToFloat() + 2 * x,
                              text.Height().ToFloat() + 2 * y);
}

// PaintRect() in highlight_painter.cc: fills the pixel-snapped rect, unless
// the rect or its snapped rect is empty.
void FillPixelSnapped(RasterCanvas& raster, const PhysicalRect& rect, const PlatformPaint& paint) {
  if (rect.size.IsEmpty()) return;
  const Rect snapped = ToPixelSnappedRect(rect);
  if (snapped.IsEmpty()) return;
  raster.DrawRect(ScalarRect::MakeXYWH(static_cast<float>(snapped.x()), static_cast<float>(snapped.y()),
                                       static_cast<float>(snapped.width()), static_cast<float>(snapped.height())),
                  paint);
}

struct RasterView {
  int viewport_height = 0; // Full-resolution framebuffer pixels.
  float scale_x = 1;
  float scale_y = 1;
  int scroll = 0; // Integer pixels in the render target.
};

// Paints the page into `raster`, a tile in render-target coordinates already
// cleared to the page color.
void PaintPage(Page& page, RasterCanvas& raster, const RasterView& view) {
  const float scale = page.scale;
  const float margin = std::round(kPageMargin * scale);
  const float accent = std::round(kAccentWidth * scale);
  const float viewport_scroll = view.scroll / view.scale_y;
  // Keep layout and text snapping in page coordinates, and scroll by integer
  // render-target pixels. This also makes cached rows reusable at low density.
  raster.Translate(0, -static_cast<float>(view.scroll));
  raster.Scale(view.scale_x, view.scale_y);
  CanvasPaintCanvas canvas(&raster);
  for (Card& card : page.cards) {
    const float top = card.top;
    // Keep the full viewport's card set when repainting a scroll strip, so
    // effects from outside the strip are handled by the raster/filter clip.
    const float viewport_top = card.top - viewport_scroll;
    if (viewport_top > view.viewport_height || viewport_top + card.height < 0) continue;
    const ScalarRect bounds = ScalarRect::MakeXYWH(margin, top, page.width - 2 * margin, card.height);
    ScalarPath background;
    background.AddRRect(bounds, 10 * scale, 10 * scale);
    PlatformPaint fill(kCardColor);
    fill.SetAntiAlias(true);
    raster.DrawPath(background, fill);
    ScalarPath bar;
    bar.AddRRect(ScalarRect::MakeXYWH(margin, top, accent * 2, card.height), accent, accent);
    PlatformPaint accent_paint(card.accent);
    accent_paint.SetAntiAlias(true);
    raster.Save();
    raster.ClipRect(ScalarRect::MakeXYWH(margin, top, accent, card.height), true);
    raster.DrawPath(bar, accent_paint);
    raster.Restore();
    // The HTML panel, its selection, then its text.
    ScalarPath panel;
    panel.AddRRect(SourcePanel(card, scale), 6 * scale, 6 * scale);
    PlatformPaint panel_paint(kSourceColor);
    panel_paint.SetAntiAlias(true);
    raster.DrawPath(panel, panel_paint);
    const SourceSelection& selection = page.selection;
    if (selection.card >= 0 && &card == &page.cards[static_cast<wtf_size_t>(selection.card)] &&
        selection.anchor != selection.focus) {
      const InlineNodeId text = card.source_text->Id();
      const InlineSelection range{{text, selection.anchor}, {text, selection.focus}};
      for (const PhysicalRect& rect : card.source.context->SelectionRectsForPaint(range, card.source.offset))
        FillPixelSnapped(raster, rect, PlatformPaint(kSelectionColor));
    }
    card.source.context->Paint(&canvas, card.source.offset);
    for (Block& block : card.blocks) block.context->Paint(&canvas, block.offset);
  }
}

// The page rows whose highlight differs between the painted selection and
// the page's, as rich_text's DocumentView finds them.
example::RowDamage HighlightDamage(Page& page, const SourceSelection& painted) {
  example::RowDamage damage;
  const auto changes = [&](int index, unsigned painted_start, unsigned painted_end, unsigned next_start,
                           unsigned next_end) {
    Card& card = page.cards[static_cast<wtf_size_t>(index)];
    const InlineNodeId text = card.source_text->Id();
    example::ForChangedHighlight(painted_start, painted_end, next_start, next_end, [&](uint32_t start, uint32_t end) {
      const InlineSelection range{{text, start}, {text, end}};
      for (const PhysicalRect& rect : card.source.context->SelectionRectsForPaint(range, card.source.offset))
        damage.Add(std::floor(rect.Y().ToFloat()), std::ceil(rect.Bottom().ToFloat()));
    });
  };
  const SourceSelection& next = page.selection;
  const unsigned painted_start = std::min(painted.anchor, painted.focus), painted_end = std::max(painted.anchor, painted.focus);
  const unsigned next_start = std::min(next.anchor, next.focus), next_end = std::max(next.anchor, next.focus);
  if (painted.card == next.card) {
    if (next.card >= 0) changes(next.card, painted_start, painted_end, next_start, next_end);
  } else {
    if (painted.card >= 0) changes(painted.card, painted_start, painted_end, 0, 0);
    if (next.card >= 0) changes(next.card, 0, 0, next_start, next_end);
  }
  return damage;
}

// The page is static between width/DPI changes. Retain render-target pixels
// across scrolls and redraw only the exposed rows and the rows whose highlight
// changed. Integer scroll positions preserve the text painter's pixel snapping
// and glyph-mask cache keys.
class PageRaster {
public:
  bool Resize(int viewport_width, int viewport_height, int divisor) {
    const int width = 1 + (viewport_width - 1) / divisor;
    const int height = 1 + (viewport_height - 1) / divisor;
    const float scale_x = static_cast<float>(width) / viewport_width;
    const float scale_y = static_cast<float>(height) / viewport_height;
    const bool resized = bitmap_.GetPixmap().Width() != width || bitmap_.GetPixmap().Height() != height;
    if (resized) bitmap_ = Bitmap(ColorType::kN32, width, height);
    if (resized || view_.viewport_height != viewport_height || view_.scale_x != scale_x || view_.scale_y != scale_y)
      valid_ = false;
    view_.viewport_height = viewport_height;
    view_.scale_x = scale_x;
    view_.scale_y = scale_y;
    return resized;
  }
  bool Paint(Page& page, bool layout_changed) {
    const Pixmap& pixels = bitmap_.GetPixmap();
    const int scroll = static_cast<int>(std::round(page.scroll * view_.scale_y));
    view_.scroll = scroll;
    const int delta = scroll - scroll_;
    const int height = pixels.Height();
    bool painted = false;
    if (!valid_ || layout_changed || delta <= -height || delta >= height) {
      painted = PaintRows(page, 0, height);
    } else {
      if (delta != 0) {
        const int exposed = std::abs(delta);
        const int source_top = delta > 0 ? exposed : 0;
        const int destination_top = delta > 0 ? 0 : exposed;
        std::memmove(pixels.WritableAddr8(0, destination_top), pixels.WritableAddr8(0, source_top),
                     static_cast<std::size_t>(height - exposed) * pixels.RowBytes());
        const int strip_top = delta > 0 ? height - exposed : 0;
        painted = PaintRows(page, strip_top, strip_top + exposed);
      }
      for (const auto& rows : HighlightDamage(page, painted_).Take()) {
        const int top = static_cast<int>(std::floor(rows.top * view_.scale_y)) - scroll;
        const int bottom = static_cast<int>(std::ceil(rows.bottom * view_.scale_y)) - scroll;
        painted |= PaintRows(page, std::max(0, top), std::min(height, bottom));
      }
    }
    painted_ = page.selection;
    scroll_ = scroll;
    valid_ = true;
    return painted; // Otherwise window exposure can present the existing texture.
  }
  const Pixmap& Pixels() const { return bitmap_.GetPixmap(); }

private:
  // Repaints render-target rows [top, bottom) in bounded tiles, as rich_text
  // repaints its damage.
  bool PaintRows(Page& page, int top, int bottom) {
    if (top >= bottom) return false;
    const Pixmap& pixels = bitmap_.GetPixmap();
    example::PaintTiles(pixels, IntRect::MakeLTRB(0, top, pixels.Width(), bottom), scratch_, kPageColor,
                        [&](RasterCanvas& raster, IntRect) { PaintPage(page, raster, view_); });
    return true;
  }

  Bitmap bitmap_;
  RasterView view_;
  RasterCanvas::ScratchBuffer scratch_;
  // The selection the pixels show.
  SourceSelection painted_;
  int scroll_ = 0;
  bool valid_ = false;
};

// Headless rendering for visual inspection without opening a native window.
bool WriteBitmap(const Pixmap& pixels, const char* path) {
  FILE* file = std::fopen(path, "wb");
  if (!file) return false;
  const uint32_t size = 54 + pixels.Width() * pixels.Height() * 4;
  const uint32_t header[] = {40, static_cast<uint32_t>(pixels.Width()),
                             static_cast<uint32_t>(-pixels.Height()), 0x00200001, 0, size - 54, 0, 0, 0, 0};
  const uint32_t offset = 54, reserved = 0;
  std::fwrite("BM", 1, 2, file);
  std::fwrite(&size, 4, 1, file);
  std::fwrite(&reserved, 4, 1, file);
  std::fwrite(&offset, 4, 1, file);
  std::fwrite(header, 4, 10, file);
  for (int y = 0; y < pixels.Height(); ++y) std::fwrite(pixels.WritableAddr8(0, y), 4, pixels.Width(), file);
  const bool ok = !std::ferror(file);
  std::fclose(file);
  return ok;
}

void Present(const Pixmap& pixels, GLuint texture, int viewport_width, int viewport_height,
               bool resized, bool updated, bool linear_filter) {
  glViewport(0, 0, viewport_width, viewport_height);
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, texture);
  const GLint filter = linear_filter ? GL_LINEAR : GL_NEAREST;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  // GL_BGRA: kN32 pixels.
  if (resized) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pixels.Width(), pixels.Height(), 0, 0x80e1, GL_UNSIGNED_BYTE, pixels.Addr());
  } else if (updated) {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pixels.Width(), pixels.Height(), 0x80e1, GL_UNSIGNED_BYTE, pixels.Addr());
  }
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glBegin(GL_TRIANGLE_STRIP);
  glTexCoord2f(0, 1);
  glVertex2f(-1, -1);
  glTexCoord2f(1, 1);
  glVertex2f(1, -1);
  glTexCoord2f(0, 0);
  glVertex2f(-1, 1);
  glTexCoord2f(1, 0);
  glVertex2f(1, 1);
  glEnd();
}

Page& Get(GLFWwindow* window) {
  return *static_cast<Page*>(glfwGetWindowUserPointer(window));
}

// The cursor in page coordinates.
PhysicalOffset CursorInPage(GLFWwindow* window) {
  double x, y;
  int window_width, window_height, width, height;
  glfwGetCursorPos(window, &x, &y);
  glfwGetWindowSize(window, &window_width, &window_height);
  glfwGetFramebufferSize(window, &width, &height);
  const double scale_x = window_width > 0 ? static_cast<double>(width) / window_width : 1;
  const double scale_y = window_height > 0 ? static_cast<double>(height) / window_height : 1;
  return {LayoutUnit(static_cast<float>(x * scale_x)), LayoutUnit(static_cast<float>(y * scale_y + Get(window).scroll))};
}

// The card whose HTML panel contains `point`, or -1.
int SourceAt(const Page& page, const PhysicalOffset& point) {
  const float x = point.left.ToFloat(), y = point.top.ToFloat();
  for (wtf_size_t i = 0; i < page.cards.size(); ++i) {
    const ScalarRect panel = SourcePanel(page.cards[i], page.scale);
    if (x >= panel.left && x < panel.right && y >= panel.top && y < panel.bottom) return static_cast<int>(i);
  }
  return -1;
}

// The offset in the card's HTML nearest to `point`.
unsigned SourceOffsetAt(Card& card, const PhysicalOffset& point) {
  const InlinePosition position = card.source.context->HitTest(point - card.source.offset);
  return position.node == card.source_text->Id() ? position.offset : 0;
}

// Copies the selected HTML, or all of the card's HTML when the selection is
// collapsed.
void CopySelection(GLFWwindow* window, const Page& page) {
  if (page.selection.card < 0) return;
  const Card& card = page.cards[static_cast<wtf_size_t>(page.selection.card)];
  unsigned start = std::min(page.selection.anchor, page.selection.focus);
  unsigned end = std::max(page.selection.anchor, page.selection.focus);
  if (start == end) {
    start = 0;
    end = card.source_text->Text().length();
  }
  glfwSetClipboardString(window, card.source_text->Text().Substring(start, end - start).Utf8().c_str());
}

} // namespace

int main(int argc, char** argv) {
  int render_divisor = 1;
  bool linear_filter = false;
  const char* snapshot_path = nullptr;
  const auto usage = [] {
    std::puts("Usage: bkit_inline_text_render_example [--render-scale 1|0.5|0.25] [--linear] [--snapshot file.bmp]\n"
              "Keys: 1 = 100%, 2 = 50%, 3 = 25%, F = nearest/linear magnification, Esc = close.\n"
              "Drag over a card's HTML to select it; Ctrl+C copies the selection, or the whole HTML\n"
              "when nothing is selected, and Ctrl+A selects all of it.\n"
              "Snapshots store the selected render resolution, before magnification.");
  };
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--render-scale") == 0 && i + 1 < argc) {
      const char* value = argv[++i];
      if (std::strcmp(value, "1") == 0) render_divisor = 1;
      else if (std::strcmp(value, "0.5") == 0) render_divisor = 2;
      else if (std::strcmp(value, "0.25") == 0) render_divisor = 4;
      else { usage(); return 2; }
    } else if (std::strcmp(argv[i], "--linear") == 0) {
      linear_filter = true;
    } else if (std::strcmp(argv[i], "--snapshot") == 0 && i + 1 < argc) {
      snapshot_path = argv[++i];
    } else if (std::strcmp(argv[i], "--help") == 0) {
      usage();
      return 0;
    } else {
      usage();
      return 2;
    }
  }
  InitializeFonts();
  Page page;
  page.render_divisor = render_divisor;
  page.linear_filter = linear_filter;
  if (snapshot_path) {
    LayoutPage(page, kWindowWidth, 1);
    PageRaster raster;
    raster.Resize(kWindowWidth, static_cast<int>(std::ceil(page.height)), render_divisor);
    raster.Paint(page, true);
    return WriteBitmap(raster.Pixels(), snapshot_path) ? 0 : 1;
  }
  if (!glfwInit()) return 1;
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
  GLFWwindow* window =
      glfwCreateWindow(kWindowWidth, kWindowHeight, "bkit | inline text render", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  glfwSetWindowUserPointer(window, &page);
  glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int, int action, int mods) {
    if (action != GLFW_PRESS) return;
    Page& page = Get(w);
    if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(w, GLFW_TRUE);
    else if (key == GLFW_KEY_C && (mods & GLFW_MOD_CONTROL)) {
      CopySelection(w, page);
    } else if (key == GLFW_KEY_A && (mods & GLFW_MOD_CONTROL) && page.width > 0) {
      const int card = page.selection.card >= 0 ? page.selection.card : SourceAt(page, CursorInPage(w));
      if (card >= 0) page.Select({card, 0, page.cards[static_cast<wtf_size_t>(card)].source_text->Text().length()});
    } else if (key >= GLFW_KEY_1 && key <= GLFW_KEY_3) {
      Get(w).render_divisor = 1 << (key - GLFW_KEY_1);
      Get(w).dirty = true;
    } else if (key == GLFW_KEY_F) {
      Get(w).linear_filter = !Get(w).linear_filter;
      Get(w).dirty = true;
    }
  });
  glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int button, int action, int) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    Page& page = Get(w);
    if (action == GLFW_RELEASE) page.selecting = false;
    if (action != GLFW_PRESS || page.width == 0) return;
    const PhysicalOffset point = CursorInPage(w);
    const int card = SourceAt(page, point);
    SourceSelection selection;
    if (card >= 0) {
      const unsigned offset = SourceOffsetAt(page.cards[static_cast<wtf_size_t>(card)], point);
      selection = {card, offset, offset};
    }
    page.selecting = card >= 0;
    page.Select(selection);
  });
  glfwSetCursorPosCallback(window, [](GLFWwindow* w, double, double) {
    Page& page = Get(w);
    if (!page.selecting || page.selection.card < 0) return;
    SourceSelection selection = page.selection;
    selection.focus = SourceOffsetAt(page.cards[static_cast<wtf_size_t>(selection.card)], CursorInPage(w));
    page.Select(selection);
  });
  glfwSetScrollCallback(window, [](GLFWwindow* w, double, double y) {
    Get(w).scroll -= static_cast<float>(y) * 60 * Get(w).scale;
    Get(w).dirty = true;
  });
  glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int, int) { Get(w).dirty = true; });
  glfwSetWindowContentScaleCallback(window, [](GLFWwindow* w, float, float) { Get(w).dirty = true; });
  glfwSetWindowRefreshCallback(window, [](GLFWwindow* w) { Get(w).dirty = true; });
  GLuint texture;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  // GL_CLAMP_TO_EDGE: linear magnification must not sample the opposite edge
  // of the scrollable page. Available in the requested OpenGL 2.1 context.
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812f);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812f);
  PageRaster raster;
  double raster_ms = 0;
  while (!glfwWindowShouldClose(window)) {
    if (page.dirty) {
      page.dirty = false;
      int width, height;
      float scale_x, scale_y;
      glfwGetFramebufferSize(window, &width, &height);
      glfwGetWindowContentScale(window, &scale_x, &scale_y);
      if (width > 0 && height > 0) {
        const float scale = scale_x > 0 ? scale_x : 1;
        const bool layout_changed = page.width != width || page.scale != scale;
        LayoutPage(page, width, scale);
        page.scroll = std::clamp(page.scroll, 0.0f, std::max(0.0f, page.height - height));
        const bool resized = raster.Resize(width, height, page.render_divisor);
        const auto start = std::chrono::steady_clock::now();
        const bool updated = raster.Paint(page, layout_changed);
        if (updated) raster_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        Present(raster.Pixels(), texture, width, height, resized, updated, page.linear_filter);
        char title[256];
        std::snprintf(title, sizeof(title),
                        "bkit | %d%% %dx%d -> %dx%d | %s | 1:100%% 2:50%% 3:25%% F:filter | Last raster %.1f ms",
                        100 / page.render_divisor, raster.Pixels().Width(), raster.Pixels().Height(), width, height,
                        page.linear_filter ? "Linear" : "Nearest", raster_ms);
        glfwSetWindowTitle(window, title);
        glfwSwapBuffers(window);
      }
    }
    glfwWaitEvents();
  }
  glDeleteTextures(1, &texture);
  glfwDestroyWindow(window);
  glfwTerminate();
}
