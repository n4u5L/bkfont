// Multilingual inline layout example: the plot of Mushoku Tensei in Chinese,
// Japanese (vertical), Korean, English, Thai, Arabic and a few more scripts,
// styled with the CSS longhands the style system implements.
//
//   bkfont_mushoku_tensei_example                 opens a scrollable window
//   bkfont_mushoku_tensei_example --snapshot a.bmp renders the whole page
//   bkfont_mushoku_tensei_example --render-scale 0.5 starts at half resolution
//   Keys: 1/2/3 = 100%/50%/25% resolution, F = nearest/linear magnification.
//   --render-scale also applies to snapshots (the BMP stores the raster pixels).
//
// There is no CSS parser: every declaration is built with StyleDeclaration's
// typed setters, the values the parser would produce. Each paragraph is its
// own InlineFormattingContext (one block container), and shared declarations
// are named rules in the context's StyleSheet.
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "fonts.h"
#include "inline_layout.h"
#include "paint/path.h"
#include "style/css_identifier_value.h"
#include "style/css_numeric_literal_value.h"
#include "style/css_string_value.h"
#include "style/css_value_list.h"

namespace {

using namespace bkfont;
using P = CSSPropertyID;
using V = CSSValueID;
using Unit = CSSPrimitiveValue::UnitType;

constexpr int kWindowWidth = 1040;
constexpr int kWindowHeight = 820;
// Logical (CSS) pixels; multiplied by the device scale factor when painting.
constexpr float kPageMargin = 28;
constexpr float kCardPadding = 22;
constexpr float kCardGap = 18;
constexpr float kAccentWidth = 5;
constexpr float kBlockGap = 10;
constexpr float kVerticalBlockHeight = 330;
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

// Chains typed StyleDeclaration setters. A rejected value is reported and
// skipped, as a parser would drop an invalid declaration.
class Css {
public:
  Css& Keyword(P property, V value) {
    return Check(declaration_.SetKeyword(property, value), property);
  }
  Css& Keywords(P property, std::initializer_list<V> values) {
    return Check(declaration_.SetKeywordList(property, std::span<const V>(values.begin(), values.size())), property);
  }
  Css& Number(P property, double value) {
    return Check(declaration_.SetNumber(property, value), property);
  }
  Css& Length(P property, CSSLength value) {
    return Check(declaration_.SetLength(property, value), property);
  }
  Css& Text(P property, const char* utf8) {
    return Check(declaration_.SetString(property, String::FromUTF8(utf8)), property);
  }
  Css& Value(P property, std::shared_ptr<const CSSValue> value) {
    return Check(declaration_.Set(property, std::move(value)), property);
  }
  Css& FontFamily(std::initializer_list<const char*> names, V generic) {
    std::vector<CSSFontFamilyName> families;
    for (const char* name : names) families.push_back({CSSValueID::kInvalid, AtomicString(String::FromUTF8(name))});
    families.push_back({generic, AtomicString()});
    return Check(declaration_.SetFontFamily(families), P::kFontFamily);
  }
  Css& TextColor(StyleColorValue color) {
    return Check(declaration_.SetColor(color), P::kColor);
  }
  Css& FillColor(StyleColorValue color) {
    return Check(declaration_.SetTextFillColor(color), P::kWebkitTextFillColor);
  }
  Css& StrokeColor(StyleColorValue color) {
    return Check(declaration_.SetTextStrokeColor(color), P::kWebkitTextStrokeColor);
  }
  Css& DecorationColor(StyleColorValue color) {
    return Check(declaration_.SetTextDecorationColor(color), P::kTextDecorationColor);
  }
  Css& EmphasisColor(StyleColorValue color) {
    return Check(declaration_.SetTextEmphasisColor(color), P::kTextEmphasisColor);
  }
  Css& DecorationLine(TextDecorationLine line) {
    return Check(declaration_.SetTextDecorationLine(line), P::kTextDecorationLine);
  }
  Css& LineHeight(double number) {
    return Check(declaration_.SetLineHeight(CSSLineHeight::Number(number)), P::kLineHeight);
  }
  Css& LetterSpacing(CSSLength value) {
    return Check(declaration_.SetLetterSpacing(value), P::kLetterSpacing);
  }
  Css& WordSpacing(CSSLength value) {
    return Check(declaration_.SetWordSpacing(value), P::kWordSpacing);
  }
  Css& TabSize(double spaces) {
    return Check(declaration_.SetTabSize(spaces), P::kTabSize);
  }
  Css& WhiteSpace(EWhiteSpace value) {
    return Check(declaration_.SetWhiteSpace(value), P::kWhiteSpace);
  }
  Css& TextWrapMode(bkfont::TextWrapMode value) {
    return Check(declaration_.SetTextWrapMode(value), P::kTextWrapMode);
  }
  Css& Oblique(double degrees) {
    return Check(declaration_.SetFontStyleOblique({degrees, Unit::kDegrees}), P::kFontStyle);
  }
  Css& Shadow(std::initializer_list<CSSTextShadow> shadows) {
    return Check(declaration_.SetTextShadow(std::span<const CSSTextShadow>(shadows.begin(), shadows.size())),
                 P::kTextShadow);
  }
  Css& Features(std::initializer_list<std::pair<const char*, int>> tags) {
    std::vector<std::pair<AtomicString, int>> features;
    for (const auto& [tag, value] : tags) features.emplace_back(AtomicString(tag), value);
    return Check(declaration_.SetFontFeatureSettings(features), P::kFontFeatureSettings);
  }
  Css& Variations(std::initializer_list<std::pair<const char*, double>> axes) {
    std::vector<std::pair<AtomicString, double>> variations;
    for (const auto& [tag, value] : axes) variations.emplace_back(AtomicString(tag), value);
    return Check(declaration_.SetFontVariationSettings(variations), P::kFontVariationSettings);
  }
  Css& Locale(const char* tag) {
    return Check(declaration_.SetLocale(String(tag)), P::kWebkitLocale);
  }
  // The `font` shorthand from some of its longhands; the others reset.
  Css& Font(CSSLength size, const char* family, V generic) {
    const StyleDeclaration::Entry longhands[] = {
        {P::kFontSize, CSSNumericLiteralValue::Create(size.value, size.unit)},
        {P::kFontFamily, CSSValueList::CreateCommaSeparated(
                             {CSSFontFamilyValue::Create(AtomicString(family)), CSSIdentifierValue::Create(generic)})},
    };
    return Check(declaration_.SetShorthand(P::kFont, longhands), P::kFont);
  }
  Css& Merge(const Css& other) {
    declaration_.Merge(other.declaration_);
    return *this;
  }
  const StyleDeclaration& Declaration() const {
    return declaration_;
  }

private:
  Css& Check(bool ok, P property) {
    if (!ok) {
      const CSSPropertyMetadata* metadata = GetCSSPropertyMetadata(property);
      std::fprintf(stderr, "rejected declaration: %.*s\n", metadata ? static_cast<int>(metadata->name.size()) : 1,
                   metadata ? metadata->name.data() : "?");
    }
    return *this;
  }
  StyleDeclaration declaration_;
};

// One paragraph, laid out as its own block container.
struct Block {
  std::unique_ptr<InlineFormattingContext> context;
  PhysicalOffset offset;
  PhysicalSize size;
};

struct Card {
  ColorARGB accent;
  bool vertical = false;
  // The language label and the CSS used, above the text blocks.
  Block header;
  std::vector<Block> blocks;
  float top = 0;
  float height = 0;
};

// Rules shared by every block. Names replace selectors; a node lists the
// rules it matches, in cascade order, before its own declarations.
void InstallRules(InlineFormattingContext& context) {
  auto& sheet = context.StyleSheet();
  (void)sheet.SetRule(AtomicString("label"), Css()
                                                 .Number(P::kFontWeight, 700)
                                                 .Keyword(P::kTextTransform, V::kUppercase)
                                                 .LetterSpacing(Em(0.08))
                                                 .Declaration());
  (void)sheet.SetRule(AtomicString("caption"), Css()
                                                   .Font(Px(12), "Cascadia Mono", V::kMonospace)
                                                   .TextColor(Rgb(0x6b7280))
                                                   .Keyword(P::kFontVariantLigatures, V::kNone)
                                                   .Declaration());
  (void)sheet.SetRule(AtomicString("title"),
                      Css().Number(P::kFontWeight, 700).Length(P::kFontSize, Em(1.35)).TextColor(Rgb(0x1f2937)).Declaration());
  (void)sheet.SetRule(AtomicString("name"), Css().Number(P::kFontWeight, 600).TextColor(Rgb(0x9a3412)).Declaration());
}

std::unique_ptr<InlineFormattingContext> NewContext(const Css& root) {
  auto context = std::make_unique<InlineFormattingContext>(Settings(), nullptr);
  InstallRules(*context);
  context->SetInlineStyle(context->RootObject(), root.Declaration());
  return context;
}

InlineFormattingContext& AddBlock(Card& card, const Css& root) {
  card.blocks.push_back(Block{NewContext(root)});
  return *card.blocks.back().context;
}

void Text(InlineFormattingContext& context, const InlineObject& parent, const char* utf8) {
  context.AppendText(parent, String::FromUTF8(utf8));
}

const InlineObject& Span(InlineFormattingContext& context, const InlineObject& parent, const char* utf8,
                         const Css& css, std::initializer_list<const char*> rules = {}) {
  const InlineObject& span = context.AppendInline(parent);
  if (rules.size()) {
    Vector<AtomicString> names;
    for (const char* rule : rules) names.push_back(AtomicString(rule));
    context.SetRules(span, std::move(names));
  }
  context.SetInlineStyle(span, css.Declaration());
  if (utf8) Text(context, span, utf8);
  return span;
}

const InlineObject& Span(InlineFormattingContext& context, const InlineObject& parent, const char* utf8,
                         std::initializer_list<const char*> rules) {
  return Span(context, parent, utf8, Css(), rules);
}

Card NewCard(ColorARGB accent, const char* label, const char* caption) {
  Card card{accent};
  card.header.context = NewContext(Css()
                                       .FontFamily({"Segoe UI Variable Text", "Segoe UI"}, V::kSansSerif)
                                       .Length(P::kFontSize, Px(13))
                                       .LineHeight(1.5)
                                       .Keyword(P::kFontOpticalSizing, V::kAuto)
                                       .TextColor(Rgb(0x374151)));
  auto& header = *card.header.context;
  const auto& root = header.RootObject();
  Span(header, root, label, Css().TextColor(Rgb(accent & 0xffffff)), {"label"});
  Text(header, root, "   ");
  Span(header, root, caption, {"caption"});
  return card;
}

// 简体中文: justification, emphasis marks, CJK/Latin autospace, strict
// line breaking and decorated quotations.
Card ChineseCard() {
  Card card = NewCard(0xffb91c1c, "简体中文 · zh-Hans",
                      "font-family · text-align: justify · text-indent: 2em · text-emphasis · "
                      "text-decoration: underline wavy · text-underline-position: under · text-autospace · "
                      "text-spacing-trim · line-break: strict");
  const Css base = Css()
                       .FontFamily({"Microsoft YaHei", "Noto Sans CJK SC", "PingFang SC"}, V::kSansSerif)
                       .Length(P::kFontSize, Px(17))
                       .LineHeight(1.75)
                       .Locale("zh-Hans")
                       .TextColor(Rgb(0x1f2937))
                       .Keyword(P::kLineBreak, V::kStrict)
                       .Keyword(P::kTextAutospace, V::kNormal)
                       .Keyword(P::kTextSpacingTrim, V::kTrimStart);

  auto& title = AddBlock(card, Css().Merge(base).Keyword(P::kTextWrapStyle, V::kBalance));
  Span(title, title.RootObject(), "无职转生～到了异世界就拿出真本事～", Css().LetterSpacing(Em(0.06)), {"title"});

  const Css paragraph = Css().Merge(base).Keyword(P::kTextAlign, V::kJustify).Length(P::kTextIndent, Em(2));
  const Css dots = Css()
                       .Keywords(P::kTextEmphasisStyle, {V::kFilled, V::kDot})
                       .Keywords(P::kTextEmphasisPosition, {V::kUnder, V::kRight})
                       .EmphasisColor(Rgb(0xb91c1c));
  auto& p1 = AddBlock(card, paragraph);
  const auto& r1 = p1.RootObject();
  Text(p1, r1, "一名34岁、足不出户的无业男子，在被家人赶出家门的那天，为了救下几名高中生而被失控的卡车撞死。"
               "再次睁开眼时，他已成为剑与魔法世界里的婴儿——");
  Span(p1, r1, "鲁迪乌斯·格雷拉特", Css().Merge(dots), {"name"});
  Text(p1, r1, "。这部Web小说自2012年起连载于「成为小说家吧」，后来被改编为轻小说与TV动画。");

  auto& p2 = AddBlock(card, paragraph);
  const auto& r2 = p2.RootObject();
  Text(p2, r2, "带着前世的记忆，他从三岁起跟随家庭教师");
  Span(p2, r2, "洛琪希", Css().Merge(dots), {"name"});
  Text(p2, r2, "学习魔术，与青梅竹马希露菲一同长大；转移事件又将他与大小姐艾莉丝抛到魔大陆的尽头。他在心中立誓：");
  Span(p2, r2, "「这一次，我要认真地活下去。」",
       Css()
           .DecorationLine(TextDecorationLine::kUnderline)
           .Keyword(P::kTextDecorationStyle, V::kWavy)
           .DecorationColor(Rgb(0xdc2626))
           .Length(P::kTextDecorationThickness, Px(1.5))
           .Keywords(P::kTextUnderlinePosition, {V::kUnder})
           .Keyword(P::kTextDecorationSkipInk, V::kAuto)
           .Number(P::kFontWeight, 700));
  return card;
}

// 日本語: vertical-rl with tate-chū-yoko, sesame emphasis and JIS04 forms.
Card JapaneseCard() {
  Card card = NewCard(0xff7c3aed, "日本語 · ja · vertical-rl",
                      "writing-mode: vertical-rl · text-orientation: mixed · text-combine-upright: all · "
                      "text-emphasis-style: sesame · font-variant-east-asian: jis04 · font-feature-settings: \"vpal\" · "
                      "white-space: pre-line · -webkit-font-smoothing");
  card.vertical = true;
  auto& block = AddBlock(card, Css()
                                   .Keyword(P::kWritingMode, V::kVerticalRl)
                                   .Keyword(P::kTextOrientation, V::kMixed)
                                   .FontFamily({"Yu Mincho", "游明朝", "Noto Serif CJK JP", "MS Mincho"}, V::kSerif)
                                   .Length(P::kFontSize, Px(19))
                                   .LineHeight(1.85)
                                   .Locale("ja")
                                   .TextColor(Rgb(0x111827))
                                   .Keyword(P::kLineBreak, V::kStrict)
                                   .Keywords(P::kFontVariantEastAsian, {V::kJis04})
                                   .Keyword(P::kWebkitFontSmoothing, V::kAntialiased)
                                   .WhiteSpace(EWhiteSpace::kPreLine));
  const auto& root = block.RootObject();
  const Css combine = Css().Keyword(P::kTextCombineUpright, V::kAll);
  Span(block, root, "無職転生　〜異世界行ったら本気だす〜",
       Css().Features({{"vpal", 1}}).TextColor(Rgb(0x5b21b6)), {"title"});
  Text(block, root, "\n");
  Span(block, root, "34", combine);
  Text(block, root, "歳・無職・引きこもりの男は、家を追い出されたその日、トラックから高校生を庇って命を落とした。"
                    "目を覚ますと、そこは剣と魔法の異世界。彼は");
  Span(block, root, "ルーデウス・グレイラット", {"name"});
  Text(block, root, "として生まれ変わり、前世の記憶を抱えたまま、今度こそ");
  Span(block, root, "本気",
       Css().Keyword(P::kTextEmphasisStyle, V::kSesame).EmphasisColor(Rgb(0xdb2777)));
  Text(block, root, "で生きると誓う。\n家庭教師ロキシーに魔術を学び、幼なじみのシルフィと育ち、"
                    "転移事件で魔大陸へ飛ばされたエリスと共に、");
  Span(block, root, "3", combine);
  Text(block, root, "年に及ぶ帰郷の旅へ――。");
  return card;
}

// 한국어: keep-all keeps Hangul words whole; the title is balanced.
Card KoreanCard() {
  Card card = NewCard(0xff0369a1, "한국어 · ko",
                      "word-break: keep-all · text-wrap-style: balance · text-align: center · "
                      "font-style: oblique 12deg · font-weight: 300 · text-shadow");
  const Css base = Css()
                       .FontFamily({"Malgun Gothic", "맑은 고딕", "Noto Sans CJK KR"}, V::kSansSerif)
                       .Length(P::kFontSize, Px(17))
                       .LineHeight(1.7)
                       .Locale("ko")
                       .TextColor(Rgb(0x1e293b))
                       .Keyword(P::kWordBreak, V::kKeepAll)
                       .Keyword(P::kTextWrapStyle, V::kBalance)
                       .Keyword(P::kTextAlign, V::kCenter);
  auto& title = AddBlock(card, base);
  Span(title, title.RootObject(), "무직전생 ~이세계에 갔으면 최선을 다한다~",
       Css().Shadow({{Px(1), Px(2), Px(3), Rgb(0x0369a1, 90)}}), {"title"});

  auto& p = AddBlock(card, base);
  const auto& root = p.RootObject();
  Text(p, root, "서른네 살의 백수 은둔형 외톨이가 집에서 쫓겨난 날, 트럭에 치일 뻔한 학생들을 구하고 목숨을 잃는다. "
                "다시 눈을 뜬 곳은 검과 마법의 세계. 그는 ");
  Span(p, root, "루데우스 그레이랫", {"name"});
  Text(p, root, "이라는 이름의 아기로 다시 태어난다. ");
  Span(p, root, "전생의 기억을 간직한 채, 이번 인생만큼은 후회 없이 진심으로 살아가겠다고 다짐한다.",
       Css().Oblique(12).Number(P::kFontWeight, 300));
  return card;
}

// English: hyphens, ligatures, small caps, outlined and shadowed text, a
// variable font, a superscript and an unbreakable URL.
Card EnglishCard() {
  Card card = NewCard(0xff15803d, "English · en",
                      "text-transform · font-variant-caps: small-caps · font-variant-ligatures · "
                      "font-variant-numeric: oldstyle-nums · hyphens: manual · text-align-last · word-spacing · "
                      "-webkit-text-stroke · font-stretch · font-variation-settings · vertical-align: super · "
                      "overflow-wrap: anywhere · text-wrap-style: pretty · visibility: hidden");
  const Css base = Css()
                       .FontFamily({"Georgia", "Times New Roman"}, V::kSerif)
                       .Length(P::kFontSize, Px(17))
                       .LineHeight(1.6)
                       .Locale("en")
                       .TextColor(Rgb(0x1c1917))
                       .Keyword(P::kFontKerning, V::kNormal)
                       .Keyword(P::kTextRendering, V::kOptimizelegibility)
                       .Keywords(P::kFontVariantLigatures, {V::kCommonLigatures, V::kDiscretionaryLigatures})
                       .Keywords(P::kFontVariantNumeric, {V::kOldstyleNums, V::kProportionalNums});

  auto& title = AddBlock(card, base);
  const auto& title_root = title.RootObject();
  Span(title, title_root, "Mushoku Tensei",
       Css().Keyword(P::kTextTransform, V::kUppercase).LetterSpacing(Em(0.12)).TextColor(Rgb(0x14532d)), {"title"});
  Text(title, title_root, "  ");
  Span(title, title_root, "Jobless Reincarnation",
       Css().Keyword(P::kFontStyle, V::kItalic).Length(P::kFontSize, Px(21)).TextColor(Rgb(0x57534e)));

  const Css paragraph = Css()
                            .Merge(base)
                            .Keyword(P::kTextAlign, V::kJustify)
                            .Keyword(P::kTextAlignLast, V::kLeft)
                            .Keyword(P::kHyphens, V::kManual)
                            .Text(P::kHyphenateCharacter, "‐")
                            .WordSpacing(Px(1))
                            .Keyword(P::kTextWrapStyle, V::kPretty)
                            .Keyword(P::kOverflowWrap, V::kAnywhere);
  auto& p1 = AddBlock(card, paragraph);
  const auto& r1 = p1.RootObject();
  Text(p1, r1, "A thirty-four-year-old shut-in, thrown out of his family’s house, dies pushing a group of "
               "students out of the path of a speeding truck. He wakes as a baby in a world of swords and sorcery: ");
  Span(p1, r1, "Rudeus Greyrat", Css().Keyword(P::kFontVariantCaps, V::kSmallCaps).LetterSpacing(Em(0.03)),
       {"name"});
  Text(p1, r1, ", son of a swordsman and a healer. Determined not to waste his second life, he masters magic "
               "under the e­nig­mat­ic tutor ");
  Span(p1, r1, "Roxy Migurdia",
       Css()
           .FillColor(Rgb(0xffffff, 0))
           .StrokeColor(Rgb(0x1d4ed8))
           .Length(P::kWebkitTextStrokeWidth, Px(0.8))
           .Number(P::kFontWeight, 700));
  Text(p1, r1, ", grows up beside his childhood friend Sylphiette, and is flung across the world with the fiery "
               "noble ");
  Span(p1, r1, "Eris Boreas Greyrat",
       Css().TextColor(Rgb(0xb91c1c)).Shadow({{Px(0), Px(0), Px(6), Rgb(0xf97316, 160)}, {Px(1), Px(1), {}, {}}}));
  Text(p1, r1, " by the Teleport Incident");
  Span(p1, r1, "1", Css().Keyword(P::kVerticalAlign, V::kSuper).Length(P::kFontSize, Percent(70)));
  Text(p1, r1, ". Its first official, fluent translation sold dis­pro­por­tion­ate­ly well. Source: ");
  Span(p1, r1, "https://ncode.syosetu.com/n9669bk/", Css().FontFamily({"Cascadia Mono", "Consolas"}, V::kMonospace)
                                                        .Length(P::kFontSize, Px(14))
                                                        .TextColor(Rgb(0x2563eb))
                                                        .DecorationLine(TextDecorationLine::kUnderline)
                                                        .Length(P::kTextUnderlineOffset, Px(3)));

  auto& p2 = AddBlock(card, Css().Merge(base).Keyword(P::kTextAlign, V::kCenter));
  const auto& r2 = p2.RootObject();
  Span(p2, r2, "“This time, I’ll live my life to the fullest.”",
       Css()
           .Keyword(P::kFontStyle, V::kItalic)
           .Length(P::kFontSize, Px(20))
           .DecorationLine(TextDecorationLine::kUnderline | TextDecorationLine::kOverline)
           .Keyword(P::kTextDecorationStyle, V::kDouble)
           .DecorationColor(Rgb(0x16a34a)));
  Text(p2, r2, "  ");
  Span(p2, r2, "Light novel vol. 1–26 · 2014–2022",
       Css()
           .FontFamily({"Bahnschrift"}, V::kSansSerif)
           .Length(P::kFontStretch, Percent(75))
           .Variations({{"wght", 600}})
           .Keywords(P::kFontVariantNumeric, {V::kLiningNums, V::kTabularNums})
           .Length(P::kFontSize, Px(15))
           .TextColor(Rgb(0x4d7c0f)));

  auto& note = AddBlock(card, Css().Merge(base).Length(P::kFontSize, Px(13)).TextColor(Rgb(0x78716c)));
  const auto& r3 = note.RootObject();
  Text(note, r3, "1. Spoiler: ");
  Span(note, r3, "Fittoa becomes a grassland", Css().Keyword(P::kVisibility, V::kHidden));
  Text(note, r3, " — hidden with visibility: hidden, which still takes up space.");
  return card;
}

// ไทย: dictionary-based word breaking without spaces, justified.
Card ThaiCard() {
  Card card = NewCard(0xffca8a04, "ภาษาไทย · th",
                      "-webkit-locale: \"th\" · ICU dictionary line breaking · text-align: justify · "
                      "text-decoration-line: line-through · text-decoration-color · line-height: 1.9");
  const Css base = Css()
                       .FontFamily({"Leelawadee UI", "Tahoma", "Noto Sans Thai"}, V::kSansSerif)
                       .Length(P::kFontSize, Px(18))
                       .LineHeight(1.9)
                       .Locale("th")
                       .TextColor(Rgb(0x292524));
  auto& title = AddBlock(card, base);
  Span(title, title.RootObject(), "เกิดชาตินี้พี่ต้องเทพ", Css().TextColor(Rgb(0x854d0e)), {"title"});

  auto& p = AddBlock(card, Css().Merge(base).Keyword(P::kTextAlign, V::kJustify));
  const auto& root = p.RootObject();
  Text(p, root, "ชายวัยสามสิบสี่ปีผู้เก็บตัวอยู่แต่ในห้องถูกครอบครัวไล่ออกจากบ้าน "
                "และเสียชีวิตขณะช่วยนักเรียนให้พ้นจากรถบรรทุก เขาลืมตาขึ้นอีกครั้งในฐานะทารกชื่อ ");
  Span(p, root, "รูเดียส เกรย์แรต", {"name"});
  Text(p, root, " ในโลกแห่งดาบและเวทมนตร์ จาก");
  Span(p, root, "คนตกงาน", Css()
                               .DecorationLine(TextDecorationLine::kLineThrough)
                               .DecorationColor(Rgb(0xdc2626))
                               .Length(P::kTextDecorationThickness, Px(2))
                               .TextColor(Rgb(0x78716c)));
  Text(p, root, " สู่จอมเวทผู้ยิ่งใหญ่ ด้วยความทรงจำจากชาติก่อน "
                "เขาตั้งปณิธานว่าชาตินี้จะใช้ชีวิตอย่างจริงจังโดยไม่ต้องเสียใจภายหลัง");
  return card;
}

// العربية: right-to-left base direction with isolated Latin and numbers.
Card ArabicCard() {
  Card card = NewCard(0xff0f766e, "العربية · ar · rtl",
                      "direction: rtl · unicode-bidi: isolate · text-align: start · "
                      "text-decoration-style: dotted · text-underline-offset · font-size-adjust");
  const Css base = Css()
                       .Keyword(P::kDirection, V::kRtl)
                       .FontFamily({"Segoe UI", "Arial", "Noto Naskh Arabic"}, V::kSansSerif)
                       .Length(P::kFontSize, Px(19))
                       .LineHeight(1.85)
                       .Locale("ar")
                       .TextColor(Rgb(0x1f2937));
  auto& title = AddBlock(card, base);
  Span(title, title.RootObject(), "مشوكو تنساي: تناسخ العاطل عن العمل", Css().TextColor(Rgb(0x115e59)), {"title"});

  const Css ltr = Css().Keyword(P::kDirection, V::kLtr).Keyword(P::kUnicodeBidi, V::kIsolate);
  auto& p = AddBlock(card, base);
  const auto& root = p.RootObject();
  Text(p, root, "رجلٌ عاطلٌ عن العمل في الرابعة والثلاثين (");
  Span(p, root, "34", Css().Merge(ltr).Number(P::kFontSizeAdjust, 0.55));
  Text(p, root, ") طردته عائلته من البيت، فلقي حتفه وهو ينقذ طلابًا من شاحنة مسرعة. "
                "ثم فتح عينيه رضيعًا في عالمٍ من السيوف والسحر باسم ");
  Span(p, root, "روديوس غريرات", {"name"});
  Text(p, root, " (");
  Span(p, root, "Rudeus Greyrat", Css().Merge(ltr).Keyword(P::kFontStyle, V::kItalic));
  Text(p, root, "). وبذكريات حياته السابقة تعلّم السحر على يد معلّمته روكسي، ");
  Span(p, root, "وأقسم أن يعيش هذه المرة بجدّيةٍ ومن دون ندم.",
       Css()
           .DecorationLine(TextDecorationLine::kUnderline)
           .Keyword(P::kTextDecorationStyle, V::kDotted)
           .DecorationColor(Rgb(0x0f766e))
           .Length(P::kTextUnderlineOffset, Px(5)));
  return card;
}

// More scripts, one block each: Cyrillic, Devanagari, Vietnamese stacked
// diacritics and Hebrew.
Card MoreScriptsCard() {
  Card card = NewCard(0xff9333ea, "Русский · हिन्दी · Tiếng Việt · עברית",
                      "font-synthesis · font-variant-caps: all-small-caps · text-transform: capitalize · "
                      "font-variant-position: super · unicode-bidi: plaintext · letter-spacing");
  const Css base = Css().Length(P::kFontSize, Px(17)).LineHeight(1.65).TextColor(Rgb(0x1f2937));

  auto& ru = AddBlock(card, Css().Merge(base).FontFamily({"Segoe UI"}, V::kSansSerif).Locale("ru"));
  Span(ru, ru.RootObject(), "реинкарнация безработного", Css().Keyword(P::kTextTransform, V::kCapitalize), {"name"});
  Text(ru, ru.RootObject(), ": тридцатичетырёхлетний затворник перерождается в мире меча и магии под именем ");
  Span(ru, ru.RootObject(), "Рудеус Грейрат",
       Css().Keyword(P::kFontVariantCaps, V::kAllSmallCaps).Keyword(P::kFontSynthesisSmallCaps, V::kAuto));
  Text(ru, ru.RootObject(), ".");

  auto& hi = AddBlock(card, Css().Merge(base).FontFamily({"Nirmala UI", "Noto Sans Devanagari"}, V::kSansSerif)
                                .Locale("hi"));
  Text(hi, hi.RootObject(), "बेरोज़गार पुनर्जन्म: चौंतीस वर्ष का एक बेरोज़गार व्यक्ति तलवार और जादू की दुनिया में ");
  Span(hi, hi.RootObject(), "रूडियस ग्रेरैट", {"name"});
  Text(hi, hi.RootObject(), " के रूप में फिर से जन्म लेता है।");

  auto& vi = AddBlock(card, Css().Merge(base).FontFamily({"Segoe UI", "Arial"}, V::kSansSerif).Locale("vi"));
  Text(vi, vi.RootObject(), "Thất nghiệp chuyển sinh");
  Span(vi, vi.RootObject(), "WN", Css().Keyword(P::kFontVariantPosition, V::kSuper).Keyword(P::kFontSynthesisWeight,
                                                                                               V::kNone));
  Text(vi, vi.RootObject(), ": một gã thất nghiệp ba mươi tư tuổi được tái sinh thành ");
  Span(vi, vi.RootObject(), "Rudeus Greyrat", Css().LetterSpacing(Px(1.5)), {"name"});
  Text(vi, vi.RootObject(), " trong thế giới của kiếm và phép thuật.");

  auto& he = AddBlock(card, Css()
                                .Merge(base)
                                .FontFamily({"Segoe UI", "Arial"}, V::kSansSerif)
                                .Locale("he")
                                .Keyword(P::kUnicodeBidi, V::kPlaintext));
  Text(he, he.RootObject(), "גלגול נשמות של מובטל: גבר מובטל בן 34 נולד מחדש בעולם של חרבות וקסמים בתור ");
  Span(he, he.RootObject(), "רודאוס גרייראט", {"name"});
  Text(he, he.RootObject(), " (Rudeus Greyrat).");
  return card;
}

// Preserved tabs and newlines, no wrapping, tabular figures and emoji.
Card TimelineCard() {
  Card card = NewCard(0xff475569, "Timeline · 年表",
                      "white-space: pre · tab-size: 6 · text-wrap-mode: nowrap · font-variant-emoji: emoji · "
                      "font-palette: normal · font-variant-numeric: tabular-nums · font-variant-alternates");
  auto& block = AddBlock(card, Css()
                                   .FontFamily({"Cascadia Mono", "Consolas"}, V::kMonospace)
                                   .Length(P::kFontSize, Px(14))
                                   .LineHeight(1.7)
                                   .TextColor(Rgb(0x334155))
                                   .WhiteSpace(EWhiteSpace::kPre)
                                   .TabSize(6)
                                   .TextWrapMode(bkfont::TextWrapMode::kNowrap)
                                   .Keywords(P::kFontVariantNumeric, {V::kTabularNums, V::kSlashedZero})
                                   .Keywords(P::kFontVariantAlternates, {V::kHistoricalForms}));
  const auto& root = block.RootObject();
  Span(block, root, "Age\tArc\n", Css().Number(P::kFontWeight, 700));
  Text(block, root, "0\tInfancy · 幼年期 · 유년기\n"
                    "7\tChildhood · 少年期 · พบกับรอกซี่\n"
                    "10\tTeleport Incident · 転移事件 · حادثة الانتقال\n"
                    "15\tYouth · 青少年期 · Юность\n");
  Span(block, root, "⚔︎ ✨ \U0001F4D6 \U0001F9D9‍♀️ \U0001F3E0",
       Css().Keyword(P::kFontVariantEmoji, V::kEmoji).Keyword(P::kFontPalette, V::kNormal));
  return card;
}

struct Page {
  std::vector<Card> cards;
  float scale = 0;
  int width = 0;
  float height = 0;
  float scroll = 0;
  bool dirty = true;
  int render_divisor = 1;
  bool linear_filter = false;

  Page() {
    cards.push_back(ChineseCard());
    cards.push_back(JapaneseCard());
    cards.push_back(KoreanCard());
    cards.push_back(EnglishCard());
    cards.push_back(ThaiCard());
    cards.push_back(ArabicCard());
    cards.push_back(MoreScriptsCard());
    cards.push_back(TimelineCard());
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
    card.top = y;
    float cursor = y + padding;
    const PhysicalSize header = LayoutBlock(card.header, scale, inner);
    card.header.offset = PhysicalOffset(LayoutUnit(left), LayoutUnit(cursor));
    cursor += header.height.ToFloat() + std::round(kBlockGap * scale);
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

struct RasterView {
  int viewport_height = 0; // Full-resolution framebuffer pixels.
  float scale_x = 1;
  float scale_y = 1;
  int scroll = 0; // Integer pixels in the render target.
};

void PaintPage(Page& page, const Pixmap& pixels, const RasterView& view, int strip_top = 0) {
  const float scale = page.scale;
  const float margin = std::round(kPageMargin * scale);
  const float accent = std::round(kAccentWidth * scale);
  const float viewport_scroll = view.scroll / view.scale_y;
  RasterCanvas raster(pixels);
  raster.Clear(kPageColor);
  // Keep layout and text snapping in page coordinates, and scroll by integer
  // render-target pixels. This also makes cached rows reusable at low density.
  raster.Translate(0, -static_cast<float>(view.scroll) - strip_top);
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
    card.header.context->Paint(&canvas, card.header.offset);
    for (Block& block : card.blocks) block.context->Paint(&canvas, block.offset);
  }
  // RasterCanvas writes back on destruction; an explicit Flush here would
  // convert every pixel twice.
}

// The page is static between width/DPI changes. Retain render-target pixels
// across scrolls and redraw only the exposed rows. Integer scroll positions
// preserve the text painter's pixel snapping and glyph-mask cache keys.
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
    if (!valid_ || layout_changed || delta <= -height || delta >= height) {
      PaintPage(page, pixels, view_);
    } else if (delta != 0) {
      const int exposed = std::abs(delta);
      const int source_top = delta > 0 ? exposed : 0;
      const int destination_top = delta > 0 ? 0 : exposed;
      std::memmove(pixels.WritableAddr8(0, destination_top), pixels.WritableAddr8(0, source_top),
                     static_cast<std::size_t>(height - exposed) * pixels.RowBytes());
      const int strip_top = delta > 0 ? height - exposed : 0;
      const Pixmap strip(pixels.GetColorType(), pixels.Width(), exposed,
                           pixels.WritableAddr8(0, strip_top), pixels.RowBytes());
      PaintPage(page, strip, view_, strip_top);
    } else {
      return false; // Window exposure can present the existing texture.
    }
    scroll_ = scroll;
    valid_ = true;
    return true;
  }
  const Pixmap& Pixels() const { return bitmap_.GetPixmap(); }

private:
  Bitmap bitmap_;
  RasterView view_;
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

} // namespace

int main(int argc, char** argv) {
  int render_divisor = 1;
  bool linear_filter = false;
  const char* snapshot_path = nullptr;
  const auto usage = [] {
    std::puts("Usage: bkfont_mushoku_tensei_example [--render-scale 1|0.5|0.25] [--linear] [--snapshot file.bmp]\n"
              "Keys: 1 = 100%, 2 = 50%, 3 = 25%, F = nearest/linear magnification, Esc = close.\n"
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
      glfwCreateWindow(kWindowWidth, kWindowHeight, "bkfont | Mushoku Tensei multilingual", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  glfwSetWindowUserPointer(window, &page);
  glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(w, GLFW_TRUE);
    else if (key >= GLFW_KEY_1 && key <= GLFW_KEY_3) {
      Get(w).render_divisor = 1 << (key - GLFW_KEY_1);
      Get(w).dirty = true;
    } else if (key == GLFW_KEY_F) {
      Get(w).linear_filter = !Get(w).linear_filter;
      Get(w).dirty = true;
    }
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
                        "bkfont | %d%% %dx%d -> %dx%d | %s | 1:100%% 2:50%% 3:25%% F:filter | Last raster %.1f ms",
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
