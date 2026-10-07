#include "style_catalog.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <optional>
#include <type_traits>
#include <variant>

namespace rich_text {
namespace {

using namespace bkit;
namespace L = css_longhand;
using V = CSSValueID;
using Unit = CSSPrimitiveValue::UnitType;

std::string_view Trim(std::string_view s) {
  while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
  while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
  return s;
}

V Keyword(std::string_view text) {
  const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
  for (int i = 1; i < kNumCSSValueKeywords; ++i) {
    const auto name = GetCSSValueName(static_cast<V>(i));
    if (name.size() == text.size() && std::equal(name.begin(), name.end(), text.begin(),
                                                 [&](char a, char b) { return lower(a) == lower(b); })) return static_cast<V>(i);
  }
  return V::kInvalid;
}

// One token of a value, as the CSS tokenizer reads it: an identifier (with
// its keyword, kInvalid when it is none), a number, a dimension or
// percentage, a hex color or a quoted string.
struct Token {
  enum class Kind { kIdent, kNumber, kDimension, kColor, kString };
  Kind kind = Kind::kIdent;
  std::string text;
  V keyword = V::kInvalid;
  double number = 0;
  bool integer = false;
  Unit unit = Unit::kNumber;
  bkit::Color color;
};

// Reads a quoted string at the start of `text`, advancing past it.
std::optional<std::string> ReadString(std::string_view& text) {
  const char quote = text.front();
  text.remove_prefix(1);
  std::string value;
  while (!text.empty()) {
    char c = text.front();
    text.remove_prefix(1);
    if (c == quote) return value;
    if (c == '\\') {
      if (text.empty()) return std::nullopt;
      c = text.front();
      text.remove_prefix(1);
    }
    if (static_cast<unsigned char>(c) < 0x20) return std::nullopt;
    value += c;
  }
  return std::nullopt;
}

std::optional<Token> ReadToken(std::string_view text) {
  Token token;
  token.text = std::string(text);
  if (text.empty()) return std::nullopt;
  if ((text.size() == 7 || text.size() == 9) && text[0] == '#') {
    uint32_t rgba = 0;
    const auto parsed = std::from_chars(text.data() + 1, text.data() + text.size(), rgba, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return std::nullopt;
    if (text.size() == 7) rgba = (rgba << 8) | 255;
    token.kind = Token::Kind::kColor;
    token.color = bkit::Color::FromRGBA(rgba >> 24, (rgba >> 16) & 255, (rgba >> 8) & 255, rgba & 255);
    return token;
  }
  double number;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
  if (parsed.ec == std::errc{}) {
    if (!std::isfinite(number) || std::abs(number) > 10000) return std::nullopt;
    const std::string_view suffix(parsed.ptr, text.data() + text.size() - parsed.ptr);
    token.number = number;
    if (suffix.empty()) {
      token.kind = Token::Kind::kNumber;
      token.integer = text.find_first_of(".eE") == std::string_view::npos;
      return token;
    }
    static constexpr std::pair<std::string_view, Unit> kUnits[] = {
        {"px", Unit::kPixels},       {"em", Unit::kEms},           {"rem", Unit::kRems},
        {"%", Unit::kPercentage},    {"pt", Unit::kPoints},        {"deg", Unit::kDegrees},
        {"rad", Unit::kRadians},     {"grad", Unit::kGradians},    {"turn", Unit::kTurns}};
    for (const auto& [name, unit] : kUnits) {
      if (suffix == name) {
        token.kind = Token::Kind::kDimension;
        token.unit = unit;
        return token;
      }
    }
    return std::nullopt;
  }
  token.kind = Token::Kind::kIdent;
  token.keyword = Keyword(text);
  return token;
}

// Space-separated tokens; quoted strings may contain spaces. Values rarely
// have more than four, which then stay in the inline buffer.
using Tokens = Vector<Token, 4>;
std::optional<Tokens> ReadTokens(std::string_view text) {
  Tokens tokens;
  text = Trim(text);
  while (!text.empty()) {
    if (text.front() == '"' || text.front() == '\'') {
      std::optional<std::string> string = ReadString(text);
      if (!string) return std::nullopt;
      Token token;
      token.kind = Token::Kind::kString;
      token.text = std::move(*string);
      tokens.push_back(std::move(token));
    } else {
      const size_t space = text.find(' ');
      std::optional<Token> token = ReadToken(text.substr(0, space));
      if (!token) return std::nullopt;
      tokens.push_back(std::move(*token));
      text.remove_prefix(space == std::string_view::npos ? text.size() : space);
    }
    text = Trim(text);
  }
  if (tokens.empty()) return std::nullopt;
  return tokens;
}

// Comma-separated parts of a list value.
Vector<std::string_view, 8> SplitCommas(std::string_view text) {
  Vector<std::string_view, 8> parts;
  while (true) {
    const size_t comma = text.find(',');
    parts.push_back(Trim(text.substr(0, comma)));
    if (comma == std::string_view::npos) return parts;
    text.remove_prefix(comma + 1);
  }
}

template <typename Variant, typename T>
struct IsAlternative : std::false_type {};
template <typename T, typename... Ts>
struct IsAlternative<std::variant<Ts...>, T> : std::bool_constant<(std::is_same_v<T, Ts> || ...)> {};

// One token as the input of a property that takes a keyword, a <number> or
// a dimension or percentage.
template <typename Input>
std::optional<Input> ScalarInput(const Token& token) {
  if constexpr (std::is_same_v<Input, V>) {
    if (token.kind == Token::Kind::kIdent) return token.keyword;
  } else {
    if constexpr (IsAlternative<Input, V>::value)
      if (token.kind == Token::Kind::kIdent) return Input(token.keyword);
    if constexpr (IsAlternative<Input, CSSNumber>::value)
      if (token.kind == Token::Kind::kNumber) return Input(CSSNumber{token.number});
    if constexpr (IsAlternative<Input, CSSLength>::value)
      if (token.kind == Token::Kind::kDimension) return Input(CSSLength{token.number, token.unit});
  }
  return std::nullopt;
}

std::optional<StyleColorValue> ColorInput(const Token& token) {
  if (token.kind == Token::Kind::kColor) return StyleColorValue(token.color);
  if (token.kind == Token::Kind::kIdent && token.keyword == V::kCurrentcolor) return StyleColorValue::CurrentColor();
  return std::nullopt;
}

template <typename Property>
bool SetScalar(StyleDeclaration& declaration, std::string_view text) {
  const auto tokens = ReadTokens(text);
  if (!tokens || tokens->size() != 1) return false;
  const auto input = ScalarInput<typename Property::Input>((*tokens)[0]);
  return input && declaration.Set<Property>(*input);
}

template <typename Property>
bool SetColor(StyleDeclaration& declaration, std::string_view text) {
  const auto tokens = ReadTokens(text);
  if (!tokens || tokens->size() != 1) return false;
  const auto color = ColorInput((*tokens)[0]);
  return color && declaration.Set<Property>(*color);
}

// A keyword, or a space-separated list of keywords
// (font-variant-numeric: tabular-nums, text-decoration-line: underline).
template <typename Property>
bool SetKeywords(StyleDeclaration& declaration, std::string_view text) {
  const auto tokens = ReadTokens(text);
  if (!tokens) return false;
  Vector<V> keywords;
  for (const Token& token : *tokens) {
    if (token.kind != Token::Kind::kIdent) return false;
    keywords.push_back(token.keyword);
  }
  // Make() has no side effects, so the list form can follow a rejected
  // single keyword.
  if (keywords.size() == 1 && declaration.Set<Property>(keywords[0])) return true;
  return declaration.Set<Property>(keywords);
}

bool SetFontFamily(StyleDeclaration& declaration, std::string_view text) {
  Vector<CSSFontFamilyName> families;
  for (std::string_view part : SplitCommas(text)) {
    if (part.empty()) return false;
    if (part.front() == '"' || part.front() == '\'') {
      std::optional<std::string> name = ReadString(part);
      if (!name || !Trim(part).empty()) return false;
      families.push_back(CSSFontFamilyName{V::kInvalid, AtomicString(String::FromUTF8(*name))});
      continue;
    }
    // An unquoted single keyword in serif ... math is a generic family.
    const V keyword = Keyword(part);
    if (keyword >= V::kSerif && keyword <= V::kMath)
      families.push_back(CSSFontFamilyName{keyword, AtomicString()});
    else
      families.push_back(CSSFontFamilyName{V::kInvalid, AtomicString(String::FromUTF8(part))});
  }
  return declaration.Set<L::FontFamily>(families);
}

bool SetFontStyle(StyleDeclaration& declaration, std::string_view text) {
  const auto tokens = ReadTokens(text);
  if (!tokens) return false;
  if (tokens->size() == 1) return SetScalar<L::FontStyle>(declaration, text);
  const Token& angle = (*tokens)[1];
  if (tokens->size() != 2 || (*tokens)[0].keyword != V::kOblique || angle.kind != Token::Kind::kDimension) return false;
  return declaration.Set<L::FontStyle>(CSSFontStyleOblique{CSSLength{angle.number, angle.unit}});
}

bool SetFontFeatureSettings(StyleDeclaration& declaration, std::string_view text) {
  if (Trim(text) == "normal") return declaration.Set<L::FontFeatureSettings>(V::kNormal);
  Vector<L::FontFeatureSettings::Feature> features;
  for (std::string_view part : SplitCommas(text)) {
    const auto tokens = ReadTokens(part);
    if (!tokens || tokens->size() > 2 || (*tokens)[0].kind != Token::Kind::kString) return false;
    L::FontFeatureSettings::Feature feature{AtomicString(String::FromUTF8((*tokens)[0].text)), {}};
    if (tokens->size() == 2) {
      const Token& value = (*tokens)[1];
      if (value.kind == Token::Kind::kNumber && value.integer)
        feature.value = CSSInteger{static_cast<int>(value.number)};
      else if (value.kind == Token::Kind::kIdent)
        feature.value = value.keyword;
      else
        return false;
    }
    features.push_back(std::move(feature));
  }
  return declaration.Set<L::FontFeatureSettings>(features);
}

bool SetFontVariationSettings(StyleDeclaration& declaration, std::string_view text) {
  if (Trim(text) == "normal") return declaration.Set<L::FontVariationSettings>(V::kNormal);
  Vector<L::FontVariationSettings::Axis> axes;
  for (std::string_view part : SplitCommas(text)) {
    const auto tokens = ReadTokens(part);
    if (!tokens || tokens->size() != 2 || (*tokens)[0].kind != Token::Kind::kString ||
        (*tokens)[1].kind != Token::Kind::kNumber)
      return false;
    axes.push_back(L::FontVariationSettings::Axis{AtomicString(String::FromUTF8((*tokens)[0].text)),
                                                  CSSNumber{(*tokens)[1].number}});
  }
  return declaration.Set<L::FontVariationSettings>(axes);
}

bool SetFontPalette(StyleDeclaration& declaration, std::string_view text) {
  const auto tokens = ReadTokens(text);
  if (!tokens || tokens->size() != 1 || (*tokens)[0].kind != Token::Kind::kIdent) return false;
  const Token& token = (*tokens)[0];
  if (token.keyword != V::kInvalid) return declaration.Set<L::FontPalette>(token.keyword);
  return declaration.Set<L::FontPalette>(AtomicString(String::FromUTF8(token.text)));
}

bool SetHyphenateCharacter(StyleDeclaration& declaration, std::string_view text) {
  text = Trim(text);
  if (text == "auto") return declaration.Set<L::HyphenateCharacter>(V::kAuto);
  if (const auto tokens = ReadTokens(text); tokens && tokens->size() == 1 && (*tokens)[0].kind == Token::Kind::kString)
    return declaration.Set<L::HyphenateCharacter>(String::FromUTF8((*tokens)[0].text));
  // The catalog also takes the character itself, unquoted.
  return declaration.Set<L::HyphenateCharacter>(String::FromUTF8(text));
}

bool SetHyphenateLimitChars(StyleDeclaration& declaration, std::string_view text) {
  const auto tokens = ReadTokens(text);
  if (!tokens) return false;
  L::HyphenateLimitChars::Input values;
  for (const Token& token : *tokens) {
    if (token.kind == Token::Kind::kNumber && token.integer)
      values.push_back(CSSInteger{static_cast<int>(token.number)});
    else if (token.kind == Token::Kind::kIdent)
      values.push_back(token.keyword);
    else
      return false;
  }
  return declaration.Set<L::HyphenateLimitChars>(values);
}

bool SetLocale(StyleDeclaration& declaration, std::string_view text) {
  text = Trim(text);
  if (text == "auto") return declaration.Set<L::WebkitLocale>(V::kAuto);
  // A BCP 47 language tag, as the lang attribute maps it.
  return declaration.Set<L::WebkitLocale>(String::FromUTF8(text));
}

bool SetTextEmphasisPosition(StyleDeclaration& declaration, std::string_view text) {
  const auto tokens = ReadTokens(text);
  if (!tokens || tokens->size() > 2) return false;
  L::TextEmphasisPosition::Input input{V::kInvalid};
  for (const Token& token : *tokens) {
    if (token.keyword == V::kOver || token.keyword == V::kUnder) {
      if (input.over_under != V::kInvalid) return false;
      input.over_under = token.keyword;
    } else {
      if (input.left_right != V::kInvalid) return false;
      input.left_right = token.keyword;
    }
  }
  return declaration.Set<L::TextEmphasisPosition>(input);
}

bool SetTextEmphasisStyle(StyleDeclaration& declaration, std::string_view text) {
  const auto tokens = ReadTokens(text);
  if (!tokens || tokens->size() > 2) return false;
  if (tokens->size() == 1) {
    const Token& token = (*tokens)[0];
    if (token.kind == Token::Kind::kString) return declaration.Set<L::TextEmphasisStyle>(String::FromUTF8(token.text));
    return declaration.Set<L::TextEmphasisStyle>(token.keyword);
  }
  // The fill and the shape in either order.
  V fill = (*tokens)[0].keyword, shape = (*tokens)[1].keyword;
  if (fill != V::kFilled && fill != V::kOpen) std::swap(fill, shape);
  return declaration.Set<L::TextEmphasisStyle>(L::TextEmphasisStyle::FillAndShape{fill, shape});
}

bool SetTextShadow(StyleDeclaration& declaration, std::string_view text) {
  if (Trim(text) == "none") return declaration.Set<L::TextShadow>(V::kNone);
  Vector<L::TextShadow::Shadow> shadows;
  for (std::string_view part : SplitCommas(text)) {
    auto tokens = ReadTokens(part);
    if (!tokens) return false;
    // The color first or last, then two or three lengths.
    std::optional<StyleColorValue> color;
    if (auto first = ColorInput(tokens->front())) {
      color = first;
      tokens->erase(tokens->begin());
    } else if (auto last = ColorInput(tokens->back())) {
      color = last;
      tokens->pop_back();
    }
    if (tokens->size() < 2 || tokens->size() > 3) return false;
    Vector<CSSLength> lengths;
    for (const Token& token : *tokens) {
      if (token.kind != Token::Kind::kDimension && !(token.kind == Token::Kind::kNumber && token.number == 0)) return false;
      // A unitless zero is a length (ConsumeLength()).
      lengths.push_back(CSSLength{token.number, token.kind == Token::Kind::kNumber ? Unit::kPixels : token.unit});
    }
    L::TextShadow::Shadow shadow{lengths[0], lengths[1], std::nullopt, color};
    if (lengths.size() == 3) shadow.blur = lengths[2];
    shadows.push_back(std::move(shadow));
  }
  return declaration.Set<L::TextShadow>(shadows);
}

bool SetTextUnderlinePosition(StyleDeclaration& declaration, std::string_view text) {
  const auto tokens = ReadTokens(text);
  if (!tokens || tokens->size() > 2) return false;
  if (tokens->size() == 1 && (*tokens)[0].keyword == V::kAuto) return declaration.Set<L::TextUnderlinePosition>(V::kAuto);
  L::TextUnderlinePosition::Parts parts;
  for (const Token& token : *tokens) {
    V& slot = token.keyword == V::kLeft || token.keyword == V::kRight ? parts.side : parts.position;
    if (slot != V::kInvalid) return false;
    slot = token.keyword;
  }
  return declaration.Set<L::TextUnderlinePosition>(parts);
}

const PropertySpec kProperties[] = {
    {"font-family", "字体", false, 0, {"serif", "sans-serif", "monospace", "cursive", "fantasy", "system-ui"}, SetFontFamily},
    {"font-size", "字号", false, 0, {"12px", "14px", "16px", "18px", "20px", "24px", "32px", "40px", "48px"}, SetScalar<L::FontSize>},
    {"font-weight", "字重", false, 0, {"400", "600", "700", "900"}, SetScalar<L::FontWeight>},
    {"font-style", "字形", false, 0, {"normal", "italic", "oblique", "oblique 14deg", "oblique -15deg"}, SetFontStyle},
    {"color", "文字颜色", false, 0, {"#243247", "#185abd", "#c43e1c", "#16836b", "#7c3aed", "#808080"}, SetColor<L::Color>},
    {"text-decoration-line", "装饰线", false, 0, {"none", "underline", "line-through", "overline", "underline line-through"}, SetKeywords<L::TextDecorationLine>},
    {"vertical-align", "上下标", false, 0, {"baseline", "super", "sub", "middle", "4px"}, SetScalar<L::VerticalAlign>},
    {"text-transform", "大小写", false, 0, {"none", "uppercase", "lowercase", "capitalize"}, SetScalar<L::TextTransform>},
    {"font-stretch", "字体宽度", false, 1, {"normal", "condensed", "expanded", "75%", "125%"}, SetScalar<L::FontStretch>},
    {"font-kerning", "字偶距", false, 1, {"auto", "normal", "none"}, SetScalar<L::FontKerning>},
    {"font-optical-sizing", "光学尺寸", false, 1, {"auto", "none"}, SetScalar<L::FontOpticalSizing>},
    {"font-size-adjust", "字体大小校正", false, 1, {"none", "0.5", "0.65"}, SetScalar<L::FontSizeAdjust>},
    {"font-variant-caps", "大写变体", false, 1, {"normal", "small-caps", "all-small-caps", "petite-caps"}, SetScalar<L::FontVariantCaps>},
    {"font-variant-ligatures", "连字", false, 1, {"normal", "none", "common-ligatures", "discretionary-ligatures"}, SetKeywords<L::FontVariantLigatures>},
    {"font-variant-numeric", "数字变体", false, 1, {"normal", "tabular-nums", "oldstyle-nums", "lining-nums tabular-nums", "diagonal-fractions"}, SetKeywords<L::FontVariantNumeric>},
    {"font-variant-east-asian", "东亚变体", false, 1, {"normal", "jis78", "jis04", "simplified", "traditional", "full-width", "proportional-width"}, SetKeywords<L::FontVariantEastAsian>},
    {"font-variant-position", "字形上下标", false, 1, {"normal", "super", "sub"}, SetScalar<L::FontVariantPosition>},
    {"font-variant-emoji", "Emoji 字形", false, 1, {"normal", "text", "emoji", "unicode"}, SetScalar<L::FontVariantEmoji>},
    {"font-feature-settings", "OpenType 特性", false, 1, {"normal", "\"liga\" 0", "\"liga\" 1", "\"ss01\" 1", "\"tnum\" 1"}, SetFontFeatureSettings},
    {"font-variation-settings", "可变字体轴", false, 1, {"normal", "\"wght\" 400", "\"wght\" 700", "\"wdth\" 75", "\"opsz\" 32"}, SetFontVariationSettings},
    {"font-synthesis-weight", "合成粗体", false, 1, {"auto", "none"}, SetScalar<L::FontSynthesisWeight>},
    {"font-synthesis-style", "合成斜体", false, 1, {"auto", "none"}, SetScalar<L::FontSynthesisStyle>},
    {"font-synthesis-small-caps", "合成小型大写", false, 1, {"auto", "none"}, SetScalar<L::FontSynthesisSmallCaps>},
    {"font-palette", "彩色字体色板", false, 1, {"normal", "light", "dark"}, SetFontPalette},
    {"text-rendering", "文本渲染", false, 1, {"auto", "optimizeSpeed", "optimizeLegibility", "geometricPrecision"}, SetScalar<L::TextRendering>},
    {"-webkit-font-smoothing", "平滑模式", false, 1, {"auto", "antialiased", "subpixel-antialiased", "none"}, SetScalar<L::WebkitFontSmoothing>},
    {"letter-spacing", "字间距", false, 1, {"normal", "1px", "2px", "4px", "-0.5px"}, SetScalar<L::LetterSpacing>},
    {"word-spacing", "词间距", false, 1, {"normal", "2px", "6px", "12px"}, SetScalar<L::WordSpacing>},
    {"text-align", "段落对齐", true, 2, {"start", "left", "center", "right", "justify"}, SetScalar<L::TextAlign>},
    {"text-align-last", "末行对齐", true, 2, {"auto", "start", "center", "end", "justify"}, SetScalar<L::TextAlignLast>},
    {"line-height", "行距", true, 2, {"1.6", "1", "1.25", "1.5", "2", "32px"}, SetScalar<L::LineHeight>},
    {"text-indent", "首行缩进", true, 2, {"0px", "2em", "24px", "48px"}, SetScalar<L::TextIndent>},
    {"white-space-collapse", "空白处理", true, 2, {"preserve", "collapse", "preserve-breaks", "break-spaces"}, SetScalar<L::WhiteSpaceCollapse>},
    {"text-wrap-mode", "自动折行", true, 2, {"wrap", "nowrap"}, SetScalar<L::TextWrapMode>},
    {"text-wrap-style", "折行策略", true, 2, {"auto", "balance", "pretty", "stable"}, SetScalar<L::TextWrapStyle>},
    {"word-break", "单词断行", true, 2, {"normal", "break-all", "keep-all", "break-word"}, SetScalar<L::WordBreak>},
    {"overflow-wrap", "溢出换行", true, 2, {"anywhere", "normal", "break-word"}, SetScalar<L::OverflowWrap>},
    {"line-break", "行首尾禁则", true, 2, {"auto", "strict", "loose", "normal", "anywhere"}, SetScalar<L::LineBreak>},
    {"hyphens", "连字符", true, 2, {"manual", "auto", "none"}, SetScalar<L::Hyphens>},
    {"hyphenate-character", "连字符字形", true, 2, {"auto", "‐", "·"}, SetHyphenateCharacter},
    {"hyphenate-limit-chars", "连字最小长度", true, 2, {"auto", "6 3 2", "8 4 3"}, SetHyphenateLimitChars},
    {"tab-size", "制表宽度", true, 2, {"4", "2", "8", "32px"}, SetScalar<L::TabSize>},
    {"direction", "阅读方向", true, 2, {"ltr", "rtl"}, SetScalar<L::Direction>},
    {"unicode-bidi", "双向文本", true, 2, {"normal", "plaintext", "isolate", "bidi-override"}, SetScalar<L::UnicodeBidi>},
    {"writing-mode", "书写方向", true, 2, {"horizontal-tb", "vertical-rl", "vertical-lr"}, SetScalar<L::WritingMode>},
    {"text-orientation", "竖排字向", true, 2, {"mixed", "upright", "sideways"}, SetScalar<L::TextOrientation>},
    {"text-spacing-trim", "标点挤压", true, 2, {"normal", "trim-start", "space-all", "space-first"}, SetScalar<L::TextSpacingTrim>},
    {"text-autospace", "中西文间距", true, 2, {"normal", "no-autospace"}, SetScalar<L::TextAutospace>},
    {"-webkit-locale", "排版语言", true, 2, {"zh-CN", "en-US", "ja", "ko", "ar", "th"}, SetLocale},
    {"text-decoration-style", "装饰线型", false, 3, {"solid", "double", "dotted", "dashed", "wavy"}, SetScalar<L::TextDecorationStyle>},
    {"text-decoration-color", "装饰线颜色", false, 3, {"currentcolor", "#185abd", "#c43e1c", "#16836b"}, SetColor<L::TextDecorationColor>},
    {"text-decoration-thickness", "装饰线粗细", false, 3, {"auto", "from-font", "1px", "2px", "3px"}, SetScalar<L::TextDecorationThickness>},
    {"text-decoration-skip-ink", "避让字形", false, 3, {"auto", "none"}, SetScalar<L::TextDecorationSkipInk>},
    {"text-underline-offset", "下划线偏移", false, 3, {"auto", "2px", "4px", "6px"}, SetScalar<L::TextUnderlineOffset>},
    {"text-underline-position", "下划线位置", false, 3, {"auto", "under", "under right", "under left"}, SetTextUnderlinePosition},
    {"text-emphasis-style", "着重号", false, 3, {"none", "filled dot", "open circle", "filled sesame", "filled triangle"}, SetTextEmphasisStyle},
    {"text-emphasis-color", "着重号颜色", false, 3, {"currentcolor", "#c43e1c", "#185abd"}, SetColor<L::TextEmphasisColor>},
    {"text-emphasis-position", "着重号位置", false, 3, {"over right", "under right", "over left"}, SetTextEmphasisPosition},
    {"text-shadow", "文字阴影", false, 3, {"none", "1px 2px 2px #7687a6", "2px 3px 0px #cbd5e1"}, SetTextShadow},
    {"-webkit-text-fill-color", "文字填充", false, 3, {"currentcolor", "#ffffff", "#185abd", "#f5c451"}, SetColor<L::WebkitTextFillColor>},
    {"-webkit-text-stroke-color", "文字描边色", false, 3, {"currentcolor", "#185abd", "#243247"}, SetColor<L::WebkitTextStrokeColor>},
    {"-webkit-text-stroke-width", "文字描边", false, 3, {"0px", "0.5px", "1px", "1.5px"}, SetScalar<L::WebkitTextStrokeWidth>},
    {"text-combine-upright", "竖排合字", false, 3, {"none", "all"}, SetScalar<L::TextCombineUpright>},
    {"visibility", "文字可见性", false, 3, {"visible", "hidden"}, SetScalar<L::Visibility>},
};

bool Apply(StyleDeclaration& declaration, const PropertySpec& spec, std::string_view text) {
  const CSSPropertyID property = CSSPropertyIDFromName(spec.name);
  if (text == "initial") return declaration.SetCSSWideKeyword(property, CSSWideKeyword::kInitial);
  if (text == "inherit") return declaration.SetCSSWideKeyword(property, CSSWideKeyword::kInherit);
  if (text == "unset") return declaration.SetCSSWideKeyword(property, CSSWideKeyword::kUnset);
  return spec.apply(declaration, text);
}

} // namespace

std::span<const PropertySpec> PropertyCatalog() {
  return kProperties;
}
const PropertySpec* FindProperty(std::string_view name) {
  for (const auto& spec : kProperties)
    if (spec.name == name) return &spec;
  return nullptr;
}
std::string PropertyValue(const Properties& properties, std::string_view name, std::string_view fallback) {
  for (const auto& [key, value] : properties)
    if (key == name) return value;
  return std::string(fallback);
}
void SetProperty(Properties& properties, std::string_view name, std::string_view value) {
  auto it = std::find_if(properties.begin(), properties.end(), [&](const auto& entry) { return entry.first == name; });
  if (value.empty()) {
    if (it != properties.end()) properties.erase(it);
  } else if (it != properties.end())
    it->second = value;
  else
    properties.emplace_back(name, value);
  std::sort(properties.begin(), properties.end());
}
bool BuildDeclaration(const Properties& properties, StyleDeclaration& out, std::string& error) {
  StyleDeclaration result;
  for (const auto& [name, value] : properties) {
    const PropertySpec* spec = FindProperty(name);
    if (!spec || value.empty() || value.size() > (name == "font-family" ? 4096u : 256u) || !Apply(result, *spec, value)) {
      error = "Unsupported CSS value: " + name + ": " + value;
      return false;
    }
  }
  out = std::move(result);
  error.clear();
  return true;
}
bkit::StyleDeclaration DefaultParagraphStyle() {
  bkit::StyleDeclaration result;
  std::string error;
  BuildDeclaration({{"font-family", "Microsoft YaHei, Segoe UI, sans-serif"}, {"font-size", "18px"}, {"color", "#243247"}, {"line-height", "1.6"}, {"white-space-collapse", "preserve"}, {"overflow-wrap", "anywhere"}, {"tab-size", "4"}, {"-webkit-locale", "zh-CN"}}, result, error);
  return result;
}

} // namespace rich_text
