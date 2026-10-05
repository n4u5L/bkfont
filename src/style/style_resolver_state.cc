// Adapted from core/css/resolver/style_resolver_state.cc.
#include "style_resolver_state.h"

namespace bkfont {

StyleResolverState::StyleResolverState(const StyleHostContext& host, const ComputedStyle* parent_style)
    : host_(host), initial_style_(host.InitialStyle()), parent_style_(parent_style),
      builder_(initial_style_, parent_style ? *parent_style : initial_style_), font_builder_(&host) {
  UpdateLengthConversionData();
}

void StyleResolverState::UpdateLengthConversionData() {
  const ComputedStyle* root = RootElementStyle();
  const FontSizeStyle style = builder_.GetFontSizeStyle();
  css_to_length_conversion_data_ = CSSToLengthConversionData(
      CSSToLengthConversionData::FontSizes(style, root),
      parent_style_ ? CSSToLengthConversionData::LineHeightSize(parent_style_->GetFontSizeStyle(), root)
                    : CSSToLengthConversionData::LineHeightSize(style, root),
      builder_.EffectiveZoom());
}

CSSToLengthConversionData StyleResolverState::UnzoomedLengthConversionData(const FontSizeStyle& font_size_style) const {
  const ComputedStyle* root_font_style = RootElementStyle();
  const CSSToLengthConversionData::FontSizes font_sizes(font_size_style, root_font_style);
  const CSSToLengthConversionData::LineHeightSize line_height_size =
      parent_style_ ? CSSToLengthConversionData::LineHeightSize(parent_style_->GetFontSizeStyle(), root_font_style)
                    : CSSToLengthConversionData::LineHeightSize(builder_.GetFontSizeStyle(), root_font_style);
  return CSSToLengthConversionData(font_sizes, line_height_size, 1);
}

CSSToLengthConversionData StyleResolverState::FontSizeConversionData() const {
  // The root's parent is the initial style.
  return UnzoomedLengthConversionData((parent_style_ ? *parent_style_ : initial_style_).GetFontSizeStyle());
}

CSSToLengthConversionData StyleResolverState::UnzoomedLengthConversionData() const {
  return UnzoomedLengthConversionData(builder_.GetFontSizeStyle());
}

void StyleResolverState::UpdateFont() {
  font_builder_.CreateFont(builder_, parent_style_);
  css_to_length_conversion_data_.SetFontSizes(
      CSSToLengthConversionData::FontSizes(builder_.GetFontSizeStyle(), RootElementStyle()));
  css_to_length_conversion_data_.SetZoom(builder_.EffectiveZoom());
}

void StyleResolverState::UpdateLineHeight() {
  css_to_length_conversion_data_.SetLineHeightSize(
      CSSToLengthConversionData::LineHeightSize(builder_.GetFontSizeStyle(), RootElementStyle()));
}

void StyleResolverState::SetEffectiveZoom(float f) {
  if (builder_.SetEffectiveZoom(f)) font_builder_.DidChangeEffectiveZoom();
}

void StyleResolverState::SetWritingMode(WritingMode new_writing_mode) {
  if (builder_.GetWritingMode() == new_writing_mode) return;
  builder_.SetWritingMode(new_writing_mode);
  UpdateLengthConversionData();
  font_builder_.DidChangeWritingMode();
}

void StyleResolverState::SetTextOrientation(ETextOrientation text_orientation) {
  if (builder_.GetTextOrientation() != text_orientation) {
    builder_.SetTextOrientation(text_orientation);
    font_builder_.DidChangeTextOrientation();
  }
}

} // namespace bkfont
