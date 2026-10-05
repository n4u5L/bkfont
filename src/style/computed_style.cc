// Adapted from core/style/computed_style.cc.
#include "computed_style.h"

#include <cassert>

#include "base/math_extras.h"
#include "base/notreached.h"
#include "base/text/case_map.h"
#include "base/text/character_names.h"
#include "base/text/math_transform.h"
#include "base/text/string_builder.h"
#include "text/capitalize.h"
#include "text/character.h"
#include "font/font_selector.h"
#include "font/simple_font_data.h"
#include "geometry/length_functions.h"
#include "layout/inline_text_metrics.h"

namespace bkfont {

std::shared_ptr<const ComputedStyle> ComputedStyle::CreateInitialStyleSingleton() {
  return std::shared_ptr<const ComputedStyle>(new ComputedStyle());
}

ComputedStyle::ComputedStyle(const ComputedStyleBuilder& builder) : ComputedStyleBase(builder) {
  // EnsureAppliedTextDecorationsCache(): the base decorations followed by
  // this decorating box's own.
  if (IsDecoratingBox()) {
    auto decorations = std::make_shared<AppliedTextDecorationVector>();
    if (const AppliedTextDecorationVector* base_decorations = BaseTextDecorationData()) {
      decorations->ReserveInitialCapacity(base_decorations->size() + 1u);
      *decorations = *base_decorations;
    }
    decorations->emplace_back(GetTextDecorationLine(), TextDecorationStyle(), ResolvedTextDecorationColor(),
                              GetTextDecorationThickness(), TextUnderlineOffset());
    applied_text_decorations_ = std::move(decorations);
  }
}

float ComputedStyle::ComputedLineHeight(const Length& line_height, const Font& font) {
  if (line_height.IsAuto()) {
    const auto* font_data = font.PrimaryFont();
    return font_data ? font_data->GetFontMetrics().LineSpacing() : 0.0f;
  }
  if (line_height.HasPercent())
    return MinimumValueForLength(line_height, LayoutUnit(font.GetFontDescription().ComputedSize())).ToFloat();
  assert(line_height.IsFixed());
  return line_height.Pixels();
}

LayoutUnit ComputedStyle::ComputedFontSizeAsFixed() const {
  return bkfont::ComputedFontSizeAsFixed(*GetFont());
}

LayoutUnit ComputedStyle::ComputedLineHeightAsFixed() const {
  return bkfont::ComputedLineHeightAsFixed(LineHeight(), *GetFont());
}

FontBaseline ComputedStyle::GetFontBaseline() const {
  // 'dominant-baseline' is always 'auto' for non-SVG elements.
  return bkfont::GetFontBaseline(GetFontDescription());
}

TextEmphasisMark ComputedStyle::GetTextEmphasisMark() const {
  TextEmphasisMark mark = TextEmphasisMarkInternal();
  if (mark != TextEmphasisMark::kAuto) return mark;
  // https://drafts.csswg.org/css-text-decor/#propdef-text-emphasis-style
  // If only `filled` or `open` is specified, the shape keyword computes to
  // `circle` in horizontal typographic modes and `sesame` in vertical
  // typographic modes.
  if (IsHorizontalTypographicMode()) return TextEmphasisMark::kDot;
  return TextEmphasisMark::kSesame;
}

namespace {

// DEFINE_STATIC_LOCAL(AtomicString, ...) for a one-character mark.
const AtomicString& MarkString(UChar character) {
  struct Marks {
    UChar character;
    AtomicString string;
  };
  static const Marks* const marks = [] {
    static const UChar characters[] = {uchar::kBullet,      uchar::kWhiteBullet,
                                       uchar::kBlackCircle, uchar::kWhiteCircle,
                                       uchar::kFisheye,     uchar::kBullseye,
                                       uchar::kBlackUpPointingTriangle, uchar::kWhiteUpPointingTriangle,
                                       uchar::kSesameDot,   uchar::kWhiteSesameDot};
    auto* result = new Marks[std::size(characters)];
    for (size_t i = 0; i < std::size(characters); ++i)
      result[i] = {characters[i], AtomicString(base::span<const UChar>(&characters[i], 1u))};
    return result;
  }();
  for (size_t i = 0;; ++i)
    if (marks[i].character == character) return marks[i].string;
}

} // namespace

const AtomicString& ComputedStyle::TextEmphasisMarkString() const {
  const bool filled = GetTextEmphasisFill() == TextEmphasisFill::kFilled;
  switch (GetTextEmphasisMark()) {
    case TextEmphasisMark::kNone: return g_null_atom;
    case TextEmphasisMark::kCustom: return TextEmphasisCustomMark();
    case TextEmphasisMark::kDot: return MarkString(filled ? uchar::kBullet : uchar::kWhiteBullet);
    case TextEmphasisMark::kCircle: return MarkString(filled ? uchar::kBlackCircle : uchar::kWhiteCircle);
    case TextEmphasisMark::kDoubleCircle: return MarkString(filled ? uchar::kFisheye : uchar::kBullseye);
    case TextEmphasisMark::kTriangle:
      return MarkString(filled ? uchar::kBlackUpPointingTriangle : uchar::kWhiteUpPointingTriangle);
    case TextEmphasisMark::kSesame: return MarkString(filled ? uchar::kSesameDot : uchar::kWhiteSesameDot);
    case TextEmphasisMark::kAuto: NOTREACHED();
  }
  NOTREACHED();
}

LineLogicalSide ComputedStyle::GetTextEmphasisLineLogicalSide() const {
  // TextEmphasisPositionAuto is not enabled upstream (experimental).
  TextEmphasisPosition position = GetTextEmphasisPosition();
  if (IsHorizontalWritingMode()) return IsOver(position) ? LineLogicalSide::kOver : LineLogicalSide::kUnder;
  if (GetWritingMode() != WritingMode::kSidewaysLr)
    return IsRight(position) ? LineLogicalSide::kOver : LineLogicalSide::kUnder;
  return IsLeft(position) ? LineLogicalSide::kOver : LineLogicalSide::kUnder;
}

Hyphenation* ComputedStyle::GetHyphenation() const {
  if (GetHyphens() != Hyphens::kAuto) return nullptr;
  if (const LayoutLocale* locale = GetFontDescription().Locale()) return locale->GetHyphenation();
  return nullptr;
}

Hyphenation* ComputedStyle::GetHyphenationWithLimits() const {
  if (Hyphenation* hyphenation = GetHyphenation()) {
    const StyleHyphenateLimitChars& limits = HyphenateLimitChars();
    hyphenation->SetLimits(limits.MinBeforeChars(), limits.MinAfterChars(), limits.MinWordChars());
    return hyphenation;
  }
  return nullptr;
}

const AtomicString& ComputedStyle::HyphenString() const {
  const AtomicString& hyphenation_string = HyphenationString();
  if (!hyphenation_string.IsNull()) return hyphenation_string;

  // FIXME: This should depend on locale.
  static const UChar kHyphenMinusCharacter = uchar::kHyphenMinus;
  static const UChar kHyphenCharacter = uchar::kHyphen;
  static const AtomicString hyphen_minus_string(base::span<const UChar>(&kHyphenMinusCharacter, 1u));
  static const AtomicString hyphen_string(base::span<const UChar>(&kHyphenCharacter, 1u));
  const SimpleFontData* primary_font = GetFont()->PrimaryFont();
  return primary_font && primary_font->GlyphForCharacter(uchar::kHyphen) ? hyphen_string : hyphen_minus_string;
}

ETextAlign ComputedStyle::GetTextAlign(bool is_last_line) const {
  if (!is_last_line) return GetTextAlign();

  // When this is the last line of a block, or the line ends with a forced line
  // break.
  // https://drafts.csswg.org/css-text-3/#propdef-text-align-last
  switch (TextAlignLast()) {
    case ETextAlignLast::kStart: return ETextAlign::kStart;
    case ETextAlignLast::kEnd: return ETextAlign::kEnd;
    case ETextAlignLast::kLeft: return ETextAlign::kLeft;
    case ETextAlignLast::kRight: return ETextAlign::kRight;
    case ETextAlignLast::kCenter: return ETextAlign::kCenter;
    case ETextAlignLast::kJustify: return ETextAlign::kJustify;
    case ETextAlignLast::kAuto: {
      ETextAlign text_align = GetTextAlign();
      if (text_align == ETextAlign::kJustify) return ETextAlign::kStart;
      return text_align;
    }
  }
  NOTREACHED();
}

// Unicode 11 introduced Georgian capital letters (U+1C90 - U+1CBA,
// U+1CB[D-F]), but virtually no font covers them. For now map them back
// to their lowercase counterparts (U+10D0 - U+10FA, U+10F[D-F]).
// https://www.unicode.org/charts/PDF/U10A0.pdf
// https://www.unicode.org/charts/PDF/U1C90.pdf
// See https://crbug.com/865427 .
static String DisableNewGeorgianCapitalLetters(const String& text) {
  if (text.IsNull() || text.Is8Bit()) return text;
  unsigned length = text.length();
  StringBuilder result;
  result.ReserveCapacity(length);
  // |text| must be well-formed UTF-16 so that there's no worry
  // about surrogate handling.
  for (unsigned i = 0; i < length; ++i) {
    UChar character = text[i];
    if (Character::IsModernGeorgianUppercase(character))
      result.Append(Character::LowercaseModernGeorgianUppercase(character));
    else
      result.Append(character);
  }
  return result.ToString();
}

namespace {

String ApplyMathAutoTransform(const String& text, TextOffsetMap* offset_map) {
  if (text.length() != 1) return text;
  UChar character = text[0];
  UChar32 transformed_char = unicode::ItalicMathVariant(text[0]);
  if (transformed_char == static_cast<UChar32>(character)) return text;

  Vector<UChar> transformed_text(U16_LENGTH(transformed_char));
  int i = 0;
  U16_APPEND_UNSAFE(transformed_text, i, transformed_char);
  String transformed_string = String(transformed_text);
  if (offset_map) offset_map->Append(text.length(), transformed_string.length());
  return transformed_string;
}

} // namespace

String ComputedStyle::ApplyTextTransform(const String& text, UChar previous_character,
                                         TextOffsetMap* offset_map) const {
  switch (TextTransform()) {
    case ETextTransform::kNone: return text;
    case ETextTransform::kCapitalize:
      // ICUCapitalization is experimental upstream.
      return Capitalize(text, previous_character);
    case ETextTransform::kUppercase: {
      const LayoutLocale* locale = GetFontDescription().Locale();
      CaseMap case_map(locale ? locale->CaseMapLocale() : CaseMap::Locale());
      return DisableNewGeorgianCapitalLetters(case_map.ToUpper(text, offset_map));
    }
    case ETextTransform::kLowercase: {
      const LayoutLocale* locale = GetFontDescription().Locale();
      CaseMap case_map(locale ? locale->CaseMapLocale() : CaseMap::Locale());
      return case_map.ToLower(text, offset_map);
    }
    case ETextTransform::kMathAuto: return ApplyMathAutoTransform(text, offset_map);
  }
  NOTREACHED();
}

TextDecorationLine ComputedStyle::TextDecorationsInEffect() const {
  TextDecorationLine decorations = GetTextDecorationLine();
  if (const auto* base_decorations = BaseTextDecorationData()) {
    for (const AppliedTextDecoration& decoration : *base_decorations) decorations |= decoration.Lines();
  }
  return decorations;
}

const AppliedTextDecorationVector& ComputedStyle::AppliedTextDecorations() const {
  static const AppliedTextDecorationVector empty;
  if (!HasAppliedTextDecorations()) return empty;
  if (!IsDecoratingBox()) return *BaseTextDecorationData();
  return *applied_text_decorations_;
}

bool ComputedStyle::TextDecorationVisualOverflowChanged(const ComputedStyle& o) const {
  const AppliedTextDecorationVector& applied_with_this = AppliedTextDecorations();
  const AppliedTextDecorationVector& applied_with_other = o.AppliedTextDecorations();
  if (applied_with_this.size() != applied_with_other.size()) return true;
  for (wtf_size_t decoration_index = 0u; decoration_index < applied_with_this.size(); ++decoration_index) {
    const AppliedTextDecoration& decoration_from_this = applied_with_this[decoration_index];
    const AppliedTextDecoration& decoration_from_other = applied_with_other[decoration_index];
    if (decoration_from_this.Thickness() != decoration_from_other.Thickness() ||
        decoration_from_this.UnderlineOffset() != decoration_from_other.UnderlineOffset() ||
        decoration_from_this.Style() != decoration_from_other.Style() ||
        decoration_from_this.Lines() != decoration_from_other.Lines())
      return true;
  }
  if (GetTextUnderlinePosition() != o.GetTextUnderlinePosition()) return true;
  return false;
}

bool ComputedStyle::operator==(const ComputedStyle& other) const {
  return InheritedEqual(other) && NonInheritedEqual(other);
}

PlatformPaint ComputedStyle::TextPaint() const {
  PlatformPaint paint = LegacyPaint();
  paint.SetColor(ResolvedTextFillColor());
  return paint;
}

PlatformPaint ComputedStyle::TextStrokePaint() const {
  PlatformPaint paint = LegacyPaint();
  paint.SetColor(ResolvedTextStrokeColor());
  paint.SetStyle(PlatformPaint::Style::kStroke);
  paint.SetStrokeWidth(TextStrokeWidth());
  return paint;
}

StyleDifference ComputedStyle::VisualInvalidationDiff(const ComputedStyle& other) const {
  StyleDifference diff;
  // css_properties.json5 "invalidate": ["reshape"] and
  // ComputedStyle::DiffNeedsReshape. Whitespace processing, text-transform,
  // bidi and wrapping changes also require recollecting inline items.
  if ((GetFont() != other.GetFont() && *GetFont() != *other.GetFont()) ||
      GetWhiteSpaceCollapse() != other.GetWhiteSpaceCollapse() || ShouldWrapLine() != other.ShouldWrapLine() ||
      Direction() != other.Direction() || GetUnicodeBidi() != other.GetUnicodeBidi() ||
      TextTransform() != other.TextTransform() || TextAutospace() != other.TextAutospace() ||
      TextCombine() != other.TextCombine() || GetWritingMode() != other.GetWritingMode() ||
      GetTextOrientation() != other.GetTextOrientation())
    diff.SetNeedsReshape();
  // "invalidate": ["layout"].
  if (EffectiveZoom() != other.EffectiveZoom() || LineHeight() != other.LineHeight() ||
      GetTabSize() != other.GetTabSize() || LegacyBaselineShift() != other.LegacyBaselineShift() ||
      WordBreak() != other.WordBreak() || GetLineBreak() != other.GetLineBreak() ||
      OverflowWrap() != other.OverflowWrap() || GetTextAlign() != other.GetTextAlign() ||
      TextAlignLast() != other.TextAlignLast() || TextIndent() != other.TextIndent() ||
      GetHyphens() != other.GetHyphens() || HyphenationString() != other.HyphenationString() ||
      HyphenateLimitChars() != other.HyphenateLimitChars() || VerticalAlign() != other.VerticalAlign() ||
      GetVerticalAlignLength() != other.GetVerticalAlignLength() || GetTextWrapStyle() != other.GetTextWrapStyle() ||
      GetTextEmphasisMark() != other.GetTextEmphasisMark() || TextEmphasisMarkString() != other.TextEmphasisMarkString() ||
      GetTextEmphasisPosition() != other.GetTextEmphasisPosition() || TextStrokeWidth() != other.TextStrokeWidth() ||
      !base::ValuesEquivalent(TextShadow(), other.TextShadow()))
    diff.SetNeedsFullLayout();
  // "invalidate": ["text-decoration"].
  if (TextDecorationVisualOverflowChanged(other)) diff.SetNeedsRecomputeVisualOverflow();
  if (TextPaint() != other.TextPaint() || Visibility() != other.Visibility() ||
      TextDecorationsInEffect() != other.TextDecorationsInEffect() ||
      AppliedTextDecorations() != other.AppliedTextDecorations() ||
      TextDecorationSkipInk() != other.TextDecorationSkipInk() ||
      ResolvedTextEmphasisColor() != other.ResolvedTextEmphasisColor() ||
      ResolvedTextStrokeColor() != other.ResolvedTextStrokeColor())
    diff.SetNeedsNormalPaintInvalidation();
  // Keep Blink's visibility:collapse transition semantics even though the
  // current inline-only layout has no collapsing table rows or flex items.
  if ((Visibility() == EVisibility::kCollapse) != (other.Visibility() == EVisibility::kCollapse))
    diff.SetNeedsFullLayout();
  return diff;
}

ComputedStyleBuilder::ComputedStyleBuilder(const ComputedStyle& style) : ComputedStyleBuilderBase(style) {}
ComputedStyleBuilder::ComputedStyleBuilder(const ComputedStyle& initial_style, const ComputedStyle& parent_style)
    : ComputedStyleBuilderBase(initial_style, parent_style) {
  SetBaseTextDecorationData(parent_style.AppliedTextDecorationData());
}
std::shared_ptr<const ComputedStyle> ComputedStyleBuilder::TakeStyle() {
  return std::shared_ptr<const ComputedStyle>(new ComputedStyle(*this));
}
std::shared_ptr<const ComputedStyle> ComputedStyleBuilder::CloneStyle() const {
  return std::shared_ptr<const ComputedStyle>(new ComputedStyle(*this));
}
void ComputedStyleBuilder::SetFontDescription(const FontDescription& description) {
  if (GetFontDescription() == description) return;
  auto* selector = GetFont()->GetFontSelector();
  SetFont(Font(description, selector ? selector->shared_from_this() : nullptr));
}
void ComputedStyleBuilder::SetColor(Color4f value) {
  if (LegacyPaint().GetColor4f() != value) MutableLegacyPaintInternal().SetColor(value);
}
void ComputedStyleBuilder::SetTabSize(const TabSize& t) {
  if (t.GetPixelSize(1) < 0) {
    if (t.IsSpaces()) SetTabSizeInternal(TabSize(0, TabSizeValueType::kSpace));
    else SetTabSizeInternal(TabSize(0, TabSizeValueType::kLength));
  } else {
    SetTabSizeInternal(t);
  }
}
bool ComputedStyleBuilder::SetEffectiveZoom(float f) {
  // Clamp the effective zoom value to a smaller (but hopeful still large
  // enough) range, to avoid overflow in derived computations.
  const float clamped_effective_zoom = ClampTo<float>(f, 1e-6f, 1e6f);
  if (EffectiveZoom() == clamped_effective_zoom) return false;
  SetEffectiveZoomInternal(clamped_effective_zoom);
  return true;
}
void ComputedStyleBuilder::SetLetterSpacing(const Length& letter_spacing) {
  FontDescription description(GetFontDescription());
  description.SetLetterSpacing(letter_spacing);
  SetFontDescription(description);
}
void ComputedStyleBuilder::SetWordSpacing(const Length& word_spacing) {
  FontDescription description(GetFontDescription());
  description.SetWordSpacing(word_spacing);
  SetFontDescription(description);
}

FontOrientation ComputedStyleBuilder::ComputeFontOrientation() const {
  // https://drafts.csswg.org/css-writing-modes/#propdef-text-orientation
  // > the property has no effect in horizontal typographic modes.
  if (IsHorizontalTypographicMode(GetWritingMode())) return FontOrientation::kHorizontal;
  switch (GetTextOrientation()) {
    case ETextOrientation::kMixed: return FontOrientation::kVerticalMixed;
    case ETextOrientation::kUpright: return FontOrientation::kVerticalUpright;
    case ETextOrientation::kSideways: return FontOrientation::kVerticalRotated;
    default: NOTREACHED();
  }
}

// Update FontOrientation in FontDescription if it is different. FontBuilder
// takes care of updating it, but if WritingMode or TextOrientation were
// changed after the style was constructed, this function synchronizes
// FontOrientation to match to this style.
void ComputedStyleBuilder::UpdateFontOrientation() {
  FontOrientation orientation = ComputeFontOrientation();
  if (GetFontDescription().Orientation() == orientation) return;
  FontDescription font_description = GetFontDescription();
  font_description.SetOrientation(orientation);
  SetFontDescription(font_description);
}

} // namespace bkfont
