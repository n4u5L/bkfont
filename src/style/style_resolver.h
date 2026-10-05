// Host replacement for StyleResolverState/CSSToLengthConversionData. Inputs
// are explicit; there is no DOM, tree scope, GC or ambient device scale.
#pragma once

#include <span>

#include "style/computed_style.h"
#include "style/style_declaration.h"

namespace bkfont {

struct StyleResolverSettings {
  explicit StyleResolverSettings(Font font) : default_font(std::move(font)) {}
  // Before zoom. Generic families, locale and the FontSelector are carried by
  // this Font until the separate FontBuilder port provides typed font setters.
  Font default_font;
  Color4f initial_color = kBlackColor4f;
};

struct StyleResolverContext {
  const ComputedStyle& initial_style;
  const ComputedStyle* parent_style = nullptr;
  const ComputedStyle* root_style = nullptr;
  float device_scale_factor = 1;
  float page_zoom_factor = 1;
  float EffectiveZoom() const { return device_scale_factor * page_zoom_factor; }
  bool IsValid() const;
};

class StyleResolver {
public:
  static std::shared_ptr<const ComputedStyle> CreateInitialStyle(
      const StyleResolverSettings&, float device_scale_factor = 1, float page_zoom_factor = 1);
  // Blocks are ordered from lowest to highest precedence. First choose the
  // winning declaration, then apply in computation order. Null blocks are
  // allowed (e.g. a named rule which is not registered yet).
  static std::shared_ptr<const ComputedStyle> Resolve(
      const StyleResolverContext&, std::span<const StyleDeclaration* const> blocks,
      const InlineStyle* legacy_style = nullptr);
};

} // namespace bkfont
