// Adapted from core/css/resolver/style_resolver.cc and style_adjuster.cc.
#include "style_resolver.h"

#include "font/font_selector.h"
#include "style/font_builder.h"
#include "style/style_cascade.h"
#include "style/style_resolver_state.h"

namespace bkit {

std::shared_ptr<const ComputedStyle> StyleResolver::CreateInitialStyle(const StyleHostContext& host) {
  // StyleResolver::InitialStyleBuilderForElement().
  ComputedStyleBuilder builder(*ComputedStyle::CreateInitialStyleSingleton());
  builder.SetEffectiveZoom(host.LayoutZoomFactor());
  FontDescription document_font_description = builder.GetFontDescription();
  const AtomicString& content_language = host.GetSettings().content_language;
  document_font_description.SetLocale(content_language.IsNull() ? nullptr : LayoutLocale::Get(content_language));
  builder.SetFontDescription(document_font_description);
  FontBuilder(&host).CreateInitialFont(builder);
  // Host extension: the initial value of 'color'.
  builder.SetColor(host.GetSettings().initial_color);
  return builder.TakeStyle();
}

std::shared_ptr<const ComputedStyle> StyleResolver::Resolve(const StyleHostContext& host,
                                                            const ComputedStyle* parent_style,
                                                            const MatchResult& match_result,
                                                            const InlineStyle* legacy, StyleAdjustInput adjust) {
  StyleResolverState state(host, parent_style);
  ComputedStyleBuilder& builder = state.StyleBuilder();
  if (legacy) {
    // Compatibility input: the legacy font keeps its own font selector.
    const InlineStyle zoomed = legacy->Zoom(builder.EffectiveZoom());
    builder.SetFont(zoomed.font);
    builder.SetLineHeight(zoomed.line_height);
    builder.SetTabSize(zoomed.tab_size);
    builder.SetLegacyPaint(zoomed.paint);
    builder.SetLegacyBaselineShift(zoomed.baseline_shift);
    // The pre-declaration API documented preserved input whitespace with
    // break-spaces wrapping; declarations below may still override it.
    builder.SetWhiteSpaceCollapse(WhiteSpaceCollapse::kBreakSpaces);
    state.UpdateLengthConversionData();
  }
  StyleCascade cascade(state, match_result);
  cascade.Apply();

  // StyleAdjuster::AdjustComputedStyle(), for the ported properties.
  if (parent_style && adjust.is_inline_content && builder.GetWritingMode() != parent_style->GetWritingMode()) {
    builder.SetWritingMode(parent_style->GetWritingMode());
    builder.SetTextOrientation(parent_style->GetTextOrientation());
    builder.UpdateFontOrientation();
  }
  // StopPropagateTextDecorations().
  if (adjust.is_atomic_inline) builder.SetBaseTextDecorationData(nullptr);
  return state.TakeStyle();
}

} // namespace bkit
