#include "style_catalog.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <sstream>

#include "style/css_color.h"
#include "style/css_identifier_value.h"
#include "style/css_numeric_literal_value.h"
#include "style/css_value_list.h"

namespace rich_text {
namespace {

using namespace bkfont;
using P = CSSPropertyID;
using V = CSSValueID;
using Unit = CSSPrimitiveValue::UnitType;

const PropertySpec kProperties[] = {
    {"font-family", "字体", false, 0, {"serif", "sans-serif", "monospace", "cursive", "fantasy", "system-ui"}},
    {"font-size", "字号", false, 0, {"12px", "14px", "16px", "18px", "20px", "24px", "32px", "40px", "48px"}},
    {"font-weight", "字重", false, 0, {"400", "600", "700", "900"}},
    {"font-style", "字形", false, 0, {"normal", "italic", "oblique", "oblique 14deg", "oblique -15deg"}},
    {"color", "文字颜色", false, 0, {"#243247", "#185abd", "#c43e1c", "#16836b", "#7c3aed", "#808080"}},
    {"text-decoration-line", "装饰线", false, 0, {"none", "underline", "line-through", "overline", "underline line-through"}},
    {"vertical-align", "上下标", false, 0, {"baseline", "super", "sub", "middle", "4px"}},
    {"text-transform", "大小写", false, 0, {"none", "uppercase", "lowercase", "capitalize"}},
    {"font-stretch", "字体宽度", false, 1, {"normal", "condensed", "expanded", "75%", "125%"}},
    {"font-kerning", "字偶距", false, 1, {"auto", "normal", "none"}},
    {"font-optical-sizing", "光学尺寸", false, 1, {"auto", "none"}},
    {"font-size-adjust", "字体大小校正", false, 1, {"none", "0.5", "0.65"}},
    {"font-variant-caps", "大写变体", false, 1, {"normal", "small-caps", "all-small-caps", "petite-caps"}},
    {"font-variant-ligatures", "连字", false, 1, {"normal", "none", "common-ligatures", "discretionary-ligatures"}},
    {"font-variant-numeric", "数字变体", false, 1, {"normal", "tabular-nums", "oldstyle-nums", "lining-nums tabular-nums", "diagonal-fractions"}},
    {"font-variant-east-asian", "东亚变体", false, 1, {"normal", "jis78", "jis04", "simplified", "traditional", "full-width", "proportional-width"}},
    {"font-variant-position", "字形上下标", false, 1, {"normal", "super", "sub"}},
    {"font-variant-emoji", "Emoji 字形", false, 1, {"normal", "text", "emoji", "unicode"}},
    {"font-feature-settings", "OpenType 特性", false, 1, {"normal", "\"liga\" 0", "\"liga\" 1", "\"ss01\" 1", "\"tnum\" 1"}},
    {"font-variation-settings", "可变字体轴", false, 1, {"normal", "\"wght\" 400", "\"wght\" 700", "\"wdth\" 75", "\"opsz\" 32"}},
    {"font-synthesis-weight", "合成粗体", false, 1, {"auto", "none"}},
    {"font-synthesis-style", "合成斜体", false, 1, {"auto", "none"}},
    {"font-synthesis-small-caps", "合成小型大写", false, 1, {"auto", "none"}},
    {"font-palette", "彩色字体色板", false, 1, {"normal", "light", "dark"}},
    {"text-rendering", "文本渲染", false, 1, {"auto", "optimizeSpeed", "optimizeLegibility", "geometricPrecision"}},
    {"-webkit-font-smoothing", "平滑模式", false, 1, {"auto", "antialiased", "subpixel-antialiased", "none"}},
    {"letter-spacing", "字间距", false, 1, {"normal", "1px", "2px", "4px", "-0.5px"}},
    {"word-spacing", "词间距", false, 1, {"normal", "2px", "6px", "12px"}},
    {"text-align", "段落对齐", true, 2, {"start", "left", "center", "right", "justify"}},
    {"text-align-last", "末行对齐", true, 2, {"auto", "start", "center", "end", "justify"}},
    {"line-height", "行距", true, 2, {"1.6", "1", "1.25", "1.5", "2", "32px"}},
    {"text-indent", "首行缩进", true, 2, {"0px", "2em", "24px", "48px"}},
    {"white-space-collapse", "空白处理", true, 2, {"preserve", "collapse", "preserve-breaks", "break-spaces"}},
    {"text-wrap-mode", "自动折行", true, 2, {"wrap", "nowrap"}},
    {"text-wrap-style", "折行策略", true, 2, {"auto", "balance", "pretty", "stable"}},
    {"word-break", "单词断行", true, 2, {"normal", "break-all", "keep-all", "break-word"}},
    {"overflow-wrap", "溢出换行", true, 2, {"anywhere", "normal", "break-word"}},
    {"line-break", "行首尾禁则", true, 2, {"auto", "strict", "loose", "normal", "anywhere"}},
    {"hyphens", "连字符", true, 2, {"manual", "auto", "none"}},
    {"hyphenate-character", "连字符字形", true, 2, {"auto", "‐", "·"}},
    {"hyphenate-limit-chars", "连字最小长度", true, 2, {"auto", "6 3 2", "8 4 3"}},
    {"tab-size", "制表宽度", true, 2, {"4", "2", "8", "32px"}},
    {"direction", "阅读方向", true, 2, {"ltr", "rtl"}},
    {"unicode-bidi", "双向文本", true, 2, {"normal", "plaintext", "isolate", "bidi-override"}},
    {"writing-mode", "书写方向", true, 2, {"horizontal-tb", "vertical-rl", "vertical-lr"}},
    {"text-orientation", "竖排字向", true, 2, {"mixed", "upright", "sideways"}},
    {"text-spacing-trim", "标点挤压", true, 2, {"normal", "trim-start", "space-all", "space-first"}},
    {"text-autospace", "中西文间距", true, 2, {"normal", "no-autospace"}},
    {"-webkit-locale", "排版语言", true, 2, {"zh-CN", "en-US", "ja", "ko", "ar", "th"}},
    {"text-decoration-style", "装饰线型", false, 3, {"solid", "double", "dotted", "dashed", "wavy"}},
    {"text-decoration-color", "装饰线颜色", false, 3, {"currentcolor", "#185abd", "#c43e1c", "#16836b"}},
    {"text-decoration-thickness", "装饰线粗细", false, 3, {"auto", "from-font", "1px", "2px", "3px"}},
    {"text-decoration-skip-ink", "避让字形", false, 3, {"auto", "none"}},
    {"text-underline-offset", "下划线偏移", false, 3, {"auto", "2px", "4px", "6px"}},
    {"text-underline-position", "下划线位置", false, 3, {"auto", "under", "under right", "under left"}},
    {"text-emphasis-style", "着重号", false, 3, {"none", "filled dot", "open circle", "filled sesame", "filled triangle"}},
    {"text-emphasis-color", "着重号颜色", false, 3, {"currentcolor", "#c43e1c", "#185abd"}},
    {"text-emphasis-position", "着重号位置", false, 3, {"over right", "under right", "over left"}},
    {"text-shadow", "文字阴影", false, 3, {"none", "1px 2px 2px #7687a6", "2px 3px 0px #cbd5e1"}},
    {"-webkit-text-fill-color", "文字填充", false, 3, {"currentcolor", "#ffffff", "#185abd", "#f5c451"}},
    {"-webkit-text-stroke-color", "文字描边色", false, 3, {"currentcolor", "#185abd", "#243247"}},
    {"-webkit-text-stroke-width", "文字描边", false, 3, {"0px", "0.5px", "1px", "1.5px"}},
    {"text-combine-upright", "竖排合字", false, 3, {"none", "all"}},
    {"visibility", "文字可见性", false, 3, {"visible", "hidden"}},
};

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
std::shared_ptr<const CSSValue> Scalar(std::string_view text) {
  if (const V keyword = Keyword(text); keyword != V::kInvalid) return CSSIdentifierValue::Create(keyword);
  if ((text.size() == 7 || text.size() == 9) && text[0] == '#') {
    uint32_t rgba = 0;
    const auto parsed = std::from_chars(text.data() + 1, text.data() + text.size(), rgba, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return nullptr;
    if (text.size() == 7) rgba = (rgba << 8) | 255;
    return cssvalue::CSSColor::Create(Color::FromRGBA(rgba >> 24, (rgba >> 16) & 255, (rgba >> 8) & 255, rgba & 255));
  }
  double number;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
  if (parsed.ec != std::errc{} || !std::isfinite(number) || std::abs(number) > 10000) return nullptr;
  const std::string_view suffix(parsed.ptr, text.data() + text.size() - parsed.ptr);
  Unit unit = Unit::kNumber;
  if (suffix == "px")
    unit = Unit::kPixels;
  else if (suffix == "em")
    unit = Unit::kEms;
  else if (suffix == "rem")
    unit = Unit::kRems;
  else if (suffix == "%")
    unit = Unit::kPercentage;
  else if (suffix == "pt")
    unit = Unit::kPoints;
  else if (suffix == "deg")
    unit = Unit::kDegrees;
  else if (suffix == "rad")
    unit = Unit::kRadians;
  else if (suffix == "grad")
    unit = Unit::kGradians;
  else if (suffix == "turn")
    unit = Unit::kTurns;
  else if (!suffix.empty())
    return nullptr;
  else if (text.find_first_of(".eE") == std::string_view::npos)
    unit = Unit::kInteger;
  return CSSNumericLiteralValue::Create(number, unit);
}

bool Apply(StyleDeclaration& declaration, P property, std::string_view text) {
  if (text == "initial") return declaration.SetCSSWideKeyword(property, CSSWideKeyword::kInitial);
  if (text == "inherit") return declaration.SetCSSWideKeyword(property, CSSWideKeyword::kInherit);
  if (text == "unset") return declaration.SetCSSWideKeyword(property, CSSWideKeyword::kUnset);
  if (property == P::kFontFamily) {
    std::vector<CSSFontFamilyName> families;
    while (!text.empty()) {
      text = Trim(text);
      if (text.empty()) return false;
      std::string name;
      const bool quoted = text.front() == '"' || text.front() == '\'';
      if (quoted) {
        const char quote = text.front();
        text.remove_prefix(1);
        bool closed = false;
        while (!text.empty()) {
          char c = text.front();
          text.remove_prefix(1);
          if (c == quote) {
            closed = true;
            break;
          }
          if (c == '\\') {
            if (text.empty()) return false;
            c = text.front();
            text.remove_prefix(1);
          }
          if (static_cast<unsigned char>(c) < 0x20) return false;
          name += c;
        }
        if (!closed) return false;
        text = Trim(text);
        if (!text.empty() && text.front() != ',') return false;
      } else {
        const size_t comma = text.find(',');
        name = Trim(text.substr(0, comma));
        text.remove_prefix(comma == std::string_view::npos ? text.size() : comma);
      }
      if (name.empty()) return false;
      const V keyword = quoted ? V::kInvalid : Keyword(name);
      if (keyword == V::kSerif || keyword == V::kSansSerif || keyword == V::kMonospace ||
          keyword == V::kCursive || keyword == V::kFantasy || keyword == V::kSystemUi)
        families.push_back({keyword, AtomicString()});
      else
        families.push_back({V::kInvalid, AtomicString(String::FromUTF8(name))});
      if (text.empty()) break;
      text.remove_prefix(1);
      if (Trim(text).empty()) return false;
    }
    return !families.empty() && declaration.SetFontFamily(families);
  }
  if (property == P::kWebkitLocale) return declaration.SetLocale(String::FromUTF8(text));
  if (property == P::kHyphenateCharacter && text != "auto") return declaration.SetString(property, String::FromUTF8(text));
  if ((property == P::kFontFeatureSettings || property == P::kFontVariationSettings) && text != "normal") {
    if (text.size() < 8 || text[0] != '"' || text[5] != '"' || text[6] != ' ') return false;
    double number;
    const auto parsed = std::from_chars(text.data() + 7, text.data() + text.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(number) ||
        std::abs(number) > 10000) return false;
    const AtomicString tag(String::FromUTF8(text.substr(1, 4)));
    if (property == P::kFontFeatureSettings) {
      if (number < 0 || number != std::floor(number)) return false;
      const std::pair<AtomicString, int> value(tag, static_cast<int>(number));
      return declaration.SetFontFeatureSettings({&value, 1});
    }
    const std::pair<AtomicString, double> value(tag, number);
    return declaration.SetFontVariationSettings({&value, 1});
  }
  if (property == P::kFontStyle && text.starts_with("oblique ")) {
    const auto angle = Scalar(Trim(text.substr(8)));
    const auto* literal = angle ? DynamicTo<CSSNumericLiteralValue>(angle.get()) : nullptr;
    return literal && literal->IsAngle() && declaration.SetFontStyleOblique({literal->DoubleValue(), literal->GetType()});
  }
  if (property == P::kTextShadow && text != "none") {
    std::istringstream stream{std::string(text)};
    std::string x, y, blur, color, extra;
    if (!(stream >> x >> y >> blur >> color) || (stream >> extra)) return false;
    const auto xv = Scalar(x), yv = Scalar(y), bv = Scalar(blur), cv = Scalar(color);
    if (!xv || !yv || !bv || !cv || !xv->IsNumericLiteralValue() || !yv->IsNumericLiteralValue() ||
        !bv->IsNumericLiteralValue() || !cv->IsColorValue()) return false;
    const auto& a = To<CSSNumericLiteralValue>(*xv);
    const auto& b = To<CSSNumericLiteralValue>(*yv);
    const auto& c = To<CSSNumericLiteralValue>(*bv);
    const CSSTextShadow shadow{{a.DoubleValue(), a.GetType()}, {b.DoubleValue(), b.GetType()}, CSSLength{c.DoubleValue(), c.GetType()}, StyleColorValue(To<cssvalue::CSSColor>(*cv).Value())};
    return declaration.SetTextShadow({&shadow, 1});
  }
  // First try an atomic value, then the space-separated typed list. Several
  // longhands require a list even for a single keyword (e.g. tabular-nums).
  if (auto value = Scalar(text); value && declaration.Set(property, value)) return true;
  CSSValueList::Values values;
  while (!text.empty()) {
    text = Trim(text);
    const size_t space = text.find(' ');
    auto value = Scalar(text.substr(0, space));
    if (!value) return false;
    values.push_back(std::move(value));
    if (space == std::string_view::npos) break;
    text.remove_prefix(space + 1);
  }
  return !values.empty() && declaration.Set(property, CSSValueList::CreateSpaceSeparated(std::move(values)));
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
    if (!FindProperty(name) || value.empty() || value.size() > (name == "font-family" ? 4096u : 256u) || !Apply(result, CSSPropertyIDFromName(name), value)) {
      error = "Unsupported CSS value: " + name + ": " + value;
      return false;
    }
  }
  out = std::move(result);
  error.clear();
  return true;
}
bkfont::StyleDeclaration DefaultParagraphStyle() {
  bkfont::StyleDeclaration result;
  std::string error;
  BuildDeclaration({{"font-family", "Microsoft YaHei, Segoe UI, sans-serif"}, {"font-size", "18px"}, {"color", "#243247"}, {"line-height", "1.6"}, {"white-space-collapse", "preserve"}, {"overflow-wrap", "anywhere"}, {"tab-size", "4"}, {"-webkit-locale", "zh-CN"}}, result, error);
  return result;
}

} // namespace rich_text
