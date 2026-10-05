// Subset of blink/renderer/core/css/resolver/style_resolver_state.h for one
// node. The builder starts from the initial value environment of the host:
// inherited fields come from the parent (the initial style for the root) and
// non-inherited fields from the initial style. Declarations on the root are
// applied on top through the cascade; they never become initial values.
#pragma once

#include <memory>

#include "style/computed_style.h"
#include "style/css_property_names.h"
#include "style/css_to_length_conversion_data.h"
#include "style/font_builder.h"
#include "style/style_host_context.h"

namespace bkfont {

class StyleResolverState {
public:
  StyleResolverState(const StyleHostContext& host, const ComputedStyle* parent_style);
  StyleResolverState(const StyleResolverState&) = delete;
  StyleResolverState& operator=(const StyleResolverState&) = delete;

  const StyleHostContext& Host() const { return host_; }
  const ComputedStyle& InitialStyle() const { return initial_style_; }
  // Null for the root. Its inherit and unset resolve to initial values.
  const ComputedStyle* ParentStyle() const { return parent_style_; }
  // documentElement()->GetComputedStyle() for other elements; null for the
  // root itself (ElementResolveContext).
  const ComputedStyle* RootElementStyle() const { return parent_style_ ? host_.RootElementStyle() : nullptr; }
  ComputedStyleBuilder& StyleBuilder() { return builder_; }
  const ComputedStyleBuilder& StyleBuilder() const { return builder_; }
  FontBuilder& GetFontBuilder() { return font_builder_; }
  // The root's parent font is the initial font.
  const FontDescription& ParentFontDescription() const {
    return (parent_style_ ? *parent_style_ : initial_style_).GetFontDescription();
  }
  bool IsInheritedForUnset(CSSPropertyID id) const { return GetCSSPropertyMetadata(id)->inherited; }

  void UpdateLengthConversionData();
  // The conversion data for font-size: the parent's font sizes, unzoomed.
  CSSToLengthConversionData FontSizeConversionData() const;
  CSSToLengthConversionData UnzoomedLengthConversionData() const;
  const CSSToLengthConversionData& CssToLengthConversionData() const { return css_to_length_conversion_data_; }
  void UpdateFont();
  void UpdateLineHeight();

  void SetEffectiveZoom(float);
  void SetWritingMode(WritingMode);
  void SetTextOrientation(ETextOrientation);

  std::shared_ptr<const ComputedStyle> TakeStyle() { return builder_.TakeStyle(); }

private:
  CSSToLengthConversionData UnzoomedLengthConversionData(const FontSizeStyle&) const;

  const StyleHostContext& host_;
  const ComputedStyle& initial_style_;
  const ComputedStyle* parent_style_;
  ComputedStyleBuilder builder_;
  FontBuilder font_builder_;
  CSSToLengthConversionData css_to_length_conversion_data_;
};

} // namespace bkfont
