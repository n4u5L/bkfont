// Grouped immutable style and COW builder, following core/style/computed_style
// and build/scripts/core/style/templates/computed_style_base.h.tmpl.
#pragma once

#include <memory>

#include "style/computed_style_base_constants.h"
#include "style/style_color.h"
#include "style/inline_style.h"
#include "style/style_difference.h"
#include "style/white_space.h"

namespace bkfont {

class ComputedStyleBuilder;

class ComputedStyle final {
public:
  const Font* GetFont() const { return &font_->font; }
  const FontDescription& GetFontDescription() const { return GetFont()->GetFontDescription(); }
  Length LineHeight() const { return inherited_->line_height; }
  const Length& SpecifiedLineHeight() const { return inherited_->line_height; }
  const TabSize& GetTabSize() const { return inherited_->tab_size; }
  float LetterSpacing() const { return GetFontDescription().LetterSpacing(); }
  float WordSpacing() const { return GetFontDescription().WordSpacing(); }
  const Length& ComputedLetterSpacing() const { return GetFontDescription().ComputedLetterSpacing(); }
  const Length& ComputedWordSpacing() const { return GetFontDescription().ComputedWordSpacing(); }
  Color4f Color() const { return inherited_->paint.GetColor4f(); }
  const StyleColorValue& TextFillColor() const { return inherited_->text_fill_color; }
  EVisibility Visibility() const { return inherited_->visibility; }
  WhiteSpaceCollapse GetWhiteSpaceCollapse() const { return inherited_->white_space_collapse; }
  bool ShouldPreserveWhiteSpaces() const { return bkfont::ShouldPreserveWhiteSpaces(GetWhiteSpaceCollapse()); }
  bool ShouldCollapseWhiteSpaces() const { return bkfont::ShouldCollapseWhiteSpaces(GetWhiteSpaceCollapse()); }
  bool ShouldPreserveBreaks() const { return bkfont::ShouldPreserveBreaks(GetWhiteSpaceCollapse()); }
  bool ShouldCollapseBreaks() const { return bkfont::ShouldCollapseBreaks(GetWhiteSpaceCollapse()); }
  bool ShouldBreakSpaces() const { return bkfont::ShouldBreakSpaces(GetWhiteSpaceCollapse()); }
  PlatformPaint TextPaint() const;
  LayoutUnit LegacyBaselineShift() const { return inherited_->baseline_shift; }
  float EffectiveZoom() const { return effective_zoom_; }
  StyleDifference VisualInvalidationDiff(const ComputedStyle&) const;
  bool operator==(const ComputedStyle&) const;
  bool InheritedEqual(const ComputedStyle&) const;
  bool HasExplicitInheritance() const { return non_inherited_->explicit_inheritance; }

private:
  friend class ComputedStyleBuilder;
  struct FontData {
    explicit FontData(Font value) : font(std::move(value)) {}
    Font font;
  };
  struct InheritedData {
    Length line_height = Length::Auto();
    TabSize tab_size{8};
    PlatformPaint paint;
    StyleColorValue text_fill_color = StyleColorValue::CurrentColor();
    EVisibility visibility = EVisibility::kVisible;
    WhiteSpaceCollapse white_space_collapse = WhiteSpaceCollapse::kCollapse;
    LayoutUnit baseline_shift;
    bool operator==(const InheritedData&) const;
  };
  // Groups for non-inherited longhands. ComputedStyleBuilder takes them from
  // the initial style, never from the parent (ComputedStyleBase::InheritFrom).
  struct NonInheritedData {
    // Resolver dependency, local to this element; never inherited itself.
    bool explicit_inheritance = false;
    bool operator==(const NonInheritedData&) const = default;
  };
  ComputedStyle(std::shared_ptr<const FontData> font, std::shared_ptr<const InheritedData> inherited,
                std::shared_ptr<const NonInheritedData> non_inherited, float zoom)
      : font_(std::move(font)), inherited_(std::move(inherited)),
        non_inherited_(std::move(non_inherited)), effective_zoom_(zoom) {}
  std::shared_ptr<const FontData> font_;
  std::shared_ptr<const InheritedData> inherited_;
  std::shared_ptr<const NonInheritedData> non_inherited_;
  float effective_zoom_;
};

class ComputedStyleBuilder {
public:
  explicit ComputedStyleBuilder(const Font& initial_font, float zoom = 1);
  // Copies every group of an existing style.
  explicit ComputedStyleBuilder(const ComputedStyle&);
  // StyleResolver's base: non-inherited groups from the initial style and
  // inherited groups from the parent, as ComputedStyleBuilder::InheritFrom().
  ComputedStyleBuilder(const ComputedStyle& initial_style, const ComputedStyle& parent_style);
  void InheritFrom(const ComputedStyle& parent_style);
  void SetHasExplicitInheritance(bool);
  const Font* GetFont() const { return &font_.Read().font; }
  Length LineHeight() const { return inherited_.Read().line_height; }
  void SetFont(const Font&);
  void SetLineHeight(const Length&);
  void SetTabSize(const TabSize&);
  void SetColor(Color4f);
  void SetTextFillColor(StyleColorValue);
  void SetVisibility(EVisibility);
  void SetWhiteSpaceCollapse(WhiteSpaceCollapse);
  void SetLegacyPaint(const PlatformPaint&);
  void SetLegacyBaselineShift(LayoutUnit);
  // Publishing freezes writable groups. Further use (or copying) of this
  // builder cannot mutate a previously published style.
  std::shared_ptr<const ComputedStyle> Build();

private:
  template <typename T> class Group {
  public:
    explicit Group(std::shared_ptr<const T> value) : source_(std::move(value)) {}
    const T& Read() const { return writable_ ? *writable_ : *source_; }
    T& Access() {
      if (!writable_) { writable_ = std::make_shared<T>(*source_); source_.reset(); }
      else if (writable_.use_count() != 1) writable_ = std::make_shared<T>(*writable_);
      return *writable_;
    }
    std::shared_ptr<const T> Freeze() {
      if (writable_) { source_ = writable_; writable_.reset(); }
      return source_;
    }
  private:
    std::shared_ptr<const T> source_;
    std::shared_ptr<T> writable_;
  };
  Group<ComputedStyle::FontData> font_;
  Group<ComputedStyle::InheritedData> inherited_;
  Group<ComputedStyle::NonInheritedData> non_inherited_;
  float effective_zoom_;
};

} // namespace bkfont
