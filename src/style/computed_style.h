// Subset of core/style/computed_style.h over the generated ComputedStyleBase.
// A published ComputedStyle is immutable; ComputedStyleBuilder is the only
// mutation interface and copies a field group on its first write to it.
#pragma once

#include <memory>

#include "base/text/text_offset_map.h"
#include "font/font_baseline.h"
#include "text/hyphenation.h"
#include "style/computed_style_base.h"
#include "style/css_to_length_conversion_data.h"
#include "style/style_difference.h"

namespace bkit {

class ComputedStyleBuilder;

class ComputedStyle final : public ComputedStyleBase {
public:
  // ComputedStyle::CreateInitialStyleSingleton(). The host keeps the style
  // instead of a process-wide singleton (no GC, no Document).
  static std::shared_ptr<const ComputedStyle> CreateInitialStyleSingleton();

  // Fonts.
  const Font* GetFont() const { return &FontInternal(); }
  const FontDescription& GetFontDescription() const { return GetFont()->GetFontDescription(); }
  float SpecifiedFontSize() const { return GetFontDescription().SpecifiedSize(); }
  float ComputedFontSize() const { return GetFontDescription().ComputedSize(); }
  LayoutUnit ComputedFontSizeAsFixed() const;
  FontSizeStyle GetFontSizeStyle() const { return FontSizeStyle(*GetFont(), SpecifiedLineHeight(), EffectiveZoom()); }
  FontBaseline GetFontBaseline() const;

  // Line height and spacing.
  Length LineHeight() const { return LineHeightInternal(); }
  const Length& SpecifiedLineHeight() const { return LineHeightInternal(); }
  static float ComputedLineHeight(const Length&, const Font&);
  float ComputedLineHeight() const { return ComputedLineHeight(LineHeight(), *GetFont()); }
  LayoutUnit ComputedLineHeightAsFixed() const;
  float LetterSpacing() const { return GetFontDescription().LetterSpacing(); }
  float WordSpacing() const { return GetFontDescription().WordSpacing(); }
  const Length& ComputedLetterSpacing() const { return GetFontDescription().ComputedLetterSpacing(); }
  const Length& ComputedWordSpacing() const { return GetFontDescription().ComputedWordSpacing(); }

  // Writing modes and direction.
  bool IsLeftToRightDirection() const { return Direction() == TextDirection::kLtr; }
  bool IsHorizontalWritingMode() const { return bkit::IsHorizontalWritingMode(GetWritingMode()); }
  bool IsHorizontalTypographicMode() const { return bkit::IsHorizontalTypographicMode(GetWritingMode()); }
  bool IsFlippedLinesWritingMode() const { return bkit::IsFlippedLinesWritingMode(GetWritingMode()); }
  bool IsFlippedBlocksWritingMode() const { return bkit::IsFlippedBlocksWritingMode(GetWritingMode()); }

  // Colors. The computed color is the color of the legacy paint; the other
  // colors resolve currentcolor against it (VisitedDependentColor()).
  Color4f Color() const { return LegacyPaint().GetColor4f(); }
  const StyleColorValue& TextFillColor() const { return ComputedStyleBase::TextFillColor(); }
  const StyleColorValue& TextStrokeColor() const { return ComputedStyleBase::TextStrokeColor(); }
  const StyleColorValue& TextDecorationColor() const { return ComputedStyleBase::TextDecorationColor(); }
  const StyleColorValue& TextEmphasisColor() const { return ComputedStyleBase::TextEmphasisColor(); }
  Color4f ResolvedTextFillColor() const { return TextFillColor().Resolve(Color()); }
  Color4f ResolvedTextStrokeColor() const { return TextStrokeColor().Resolve(Color()); }
  Color4f ResolvedTextDecorationColor() const { return TextDecorationColor().Resolve(Color()); }
  Color4f ResolvedTextEmphasisColor() const { return TextEmphasisColor().Resolve(Color()); }

  // White space and wrapping.
  EWhiteSpace WhiteSpace() const { return ToWhiteSpace(GetWhiteSpaceCollapse(), GetTextWrapMode()); }
  bool ShouldWrapLine() const { return bkit::ShouldWrapLine(GetTextWrapMode()); }
  bool ShouldPreserveWhiteSpaces() const { return bkit::ShouldPreserveWhiteSpaces(GetWhiteSpaceCollapse()); }
  bool ShouldCollapseWhiteSpaces() const { return bkit::ShouldCollapseWhiteSpaces(GetWhiteSpaceCollapse()); }
  bool ShouldPreserveBreaks() const { return bkit::ShouldPreserveBreaks(GetWhiteSpaceCollapse()); }
  bool ShouldCollapseBreaks() const { return bkit::ShouldCollapseBreaks(GetWhiteSpaceCollapse()); }
  bool ShouldBreakSpaces() const { return bkit::ShouldBreakSpaces(GetWhiteSpaceCollapse()); }
  // ComputedStyle::BreakOnlyAfterWhiteSpace().
  bool BreakOnlyAfterWhiteSpace() const {
    return ShouldPreserveWhiteSpaces() || GetLineBreak() == LineBreak::kAfterWhiteSpace;
  }
  // ComputedStyle::BreakWords().
  bool BreakWords() const {
    return (WordBreak() == EWordBreak::kBreakWord || OverflowWrap() != EOverflowWrap::kNormal) && ShouldWrapLine();
  }

  // text-align for a line; text-align-last applies to the last line of the
  // block and lines before a forced break.
  ETextAlign GetTextAlign() const { return ComputedStyleBase::GetTextAlign(); }
  ETextAlign GetTextAlign(bool is_last_line) const;

  // Hyphenation.
  Hyphenation* GetHyphenation() const;
  Hyphenation* GetHyphenationWithLimits() const;
  const AtomicString& HyphenString() const;

  // text-transform; `offset_map` receives the length changes.
  String ApplyTextTransform(const String&, UChar previous_character, TextOffsetMap* offset_map) const;

  // vertical-align.
  EVerticalAlign VerticalAlign() const { return static_cast<EVerticalAlign>(VerticalAlignInternal()); }

  // Emphasis marks.
  TextEmphasisMark GetTextEmphasisMark() const;
  const AtomicString& TextEmphasisMarkString() const;
  LineLogicalSide GetTextEmphasisLineLogicalSide() const;

  // Text decorations.
  TextDecorationLine TextDecorationsInEffect() const;
  const AppliedTextDecorationVector& AppliedTextDecorations() const;
  std::shared_ptr<const AppliedTextDecorationVector> AppliedTextDecorationData() const {
    return IsDecoratingBox() ? applied_text_decorations_ : SharedBaseTextDecorationData();
  }
  // https://drafts.csswg.org/css-text-decor-3/#decorating-box
  bool IsDecoratingBox() const { return GetTextDecorationLine() != TextDecorationLine::kNone; }
  bool HasAppliedTextDecorations() const { return IsDecoratingBox() || BaseTextDecorationData(); }
  bool TextDecorationVisualOverflowChanged(const ComputedStyle&) const;
  bool HasTextShadow() const { return TextShadow(); }

  PlatformPaint TextPaint() const;
  PlatformPaint TextStrokePaint() const;
  StyleDifference VisualInvalidationDiff(const ComputedStyle&) const;
  bool operator==(const ComputedStyle&) const;

private:
  friend class ComputedStyleBuilder;
  ComputedStyle() = default;
  explicit ComputedStyle(const ComputedStyleBuilder&);
  // StyleCachedData::applied_text_decorations_, computed eagerly.
  std::shared_ptr<const AppliedTextDecorationVector> applied_text_decorations_;
};

class ComputedStyleBuilder final : public ComputedStyleBuilderBase {
public:
  // Starts from every group of an existing style.
  explicit ComputedStyleBuilder(const ComputedStyle& style);
  // StyleResolver's base: non-inherited fields from the initial style and
  // inherited fields from the parent. Text decorations propagate from the
  // parent (ComputedStyleBuilder's constructor and StyleAdjuster).
  ComputedStyleBuilder(const ComputedStyle& initial_style, const ComputedStyle& parent_style);
  ComputedStyleBuilder(const ComputedStyleBuilder&) = delete;
  ComputedStyleBuilder(ComputedStyleBuilder&&) = default;
  ComputedStyleBuilder& operator=(const ComputedStyleBuilder&) = delete;
  ComputedStyleBuilder& operator=(ComputedStyleBuilder&&) = default;

  // Both publish the current groups as an immutable style. A later write
  // through this builder copies the group first.
  std::shared_ptr<const ComputedStyle> TakeStyle();
  std::shared_ptr<const ComputedStyle> CloneStyle() const;

  const Font* GetFont() const { return &FontInternal(); }
  const FontDescription& GetFontDescription() const { return GetFont()->GetFontDescription(); }
  FontSizeStyle GetFontSizeStyle() const { return FontSizeStyle(*GetFont(), LineHeight(), EffectiveZoom()); }
  const Length& LineHeight() const { return LineHeightInternal(); }
  Color4f Color() const { return LegacyPaint().GetColor4f(); }
  const StyleColorValue& TextFillColor() const { return ComputedStyleBuilderBase::TextFillColor(); }
  EVerticalAlign VerticalAlign() const { return static_cast<EVerticalAlign>(VerticalAlignInternal()); }
  void SetFontDescription(const FontDescription&);
  // The computed color is the color of the legacy paint.
  void SetColor(Color4f);
  void SetTabSize(const TabSize&);
  bool SetEffectiveZoom(float);
  void SetLetterSpacing(const Length&);
  void SetWordSpacing(const Length&);
  void SetVerticalAlign(EVerticalAlign v) { SetVerticalAlignInternal(static_cast<unsigned>(v)); }
  void SetVerticalAlignLength(const Length& length) {
    SetVerticalAlignInternal(static_cast<unsigned>(EVerticalAlign::kLength));
    SetVerticalAlignLengthInternal(length);
  }
  FontOrientation ComputeFontOrientation() const;
  void UpdateFontOrientation();
};

} // namespace bkit
