// Adapted from core/css/resolver/style_cascade.cc, style_builder_converter.cc
// and properties/longhands/longhands_custom.cc (Color::ApplyValue).
#include "style_resolver.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <limits>

#include "font/font_selector.h"
#include "layout/inline_text_metrics.h"
#include "style/css_color.h"
#include "style/css_identifier_value.h"
#include "style/css_math_function_value.h"
#include "style/css_numeric_literal_value.h"

namespace bkfont {
namespace {

using UnitType = CSSPrimitiveValue::UnitType;

bool ValidZoom(float device, float page) {
  return std::isfinite(device) && device > 0 && std::isfinite(page) && page > 0 &&
         std::isfinite(device * page) && device * page > 0;
}

Font WithDescription(const Font& font, const FontDescription& description) {
  auto* selector = font.GetFontSelector();
  return Font(description, selector ? selector->shared_from_this() : nullptr);
}

float ClampFloat(long double value) {
  return static_cast<float>(std::clamp(value, -static_cast<long double>(std::numeric_limits<float>::max()),
                                     static_cast<long double>(std::numeric_limits<float>::max())));
}

// Line-height itself uses the parent's lh; other lengths use the newly
// computed line-height. Root rlh in line-height uses the initial line-height.
float ConvertLength(const CSSValue& value, const StyleResolverContext& context,
                    const ComputedStyleBuilder& builder, bool computing_line_height) {
  const auto& parent = context.parent_style ? *context.parent_style : context.initial_style;
  const auto& root = context.root_style ? *context.root_style : context.initial_style;
  const Font& font = *builder.GetFont();
  const auto term_value = [&](double number, UnitType unit) -> long double {
    long double scale = 0;
    switch (unit) {
      case UnitType::kPixels: scale = context.EffectiveZoom(); break;
      case UnitType::kEms: scale = font.GetFontDescription().ComputedSize(); break;
      case UnitType::kRems: scale = root.GetFontDescription().ComputedSize(); break;
      case UnitType::kPercentage: scale = font.GetFontDescription().ComputedSize() / 100.0L; break;
      case UnitType::kLhs:
        scale = computing_line_height ? ComputedLineHeightAsFixed(parent.LineHeight(), *parent.GetFont()).ToDouble()
                                      : ComputedLineHeightAsFixed(builder.LineHeight(), font).ToDouble();
        break;
      case UnitType::kRlhs:
        // For the root element, non-line-height properties see its current lh.
        scale = !context.root_style && !computing_line_height
                    ? ComputedLineHeightAsFixed(builder.LineHeight(), font).ToDouble()
                    : ComputedLineHeightAsFixed(root.LineHeight(), *root.GetFont()).ToDouble();
        break;
      case UnitType::kUnknown:
      case UnitType::kNumber: break; // Validated separately for line-height/tabs.
    }
    return static_cast<long double>(number) * scale;
  };
  if (const auto* literal = DynamicTo<CSSNumericLiteralValue>(value))
    return ClampFloat(term_value(literal->DoubleValue(), literal->GetType()));
  long double result = 0;
  for (const auto& term : To<CSSMathFunctionValue>(value).Terms()) result += term_value(term.value, term.unit);
  return ClampFloat(result);
}

void CopyProperty(CSSPropertyID id, const ComputedStyle& source, ComputedStyleBuilder& builder) {
  switch (id) {
    case CSSPropertyID::kColor: builder.SetColor(source.Color()); break;
    case CSSPropertyID::kWebkitTextFillColor: builder.SetTextFillColor(source.TextFillColor()); break;
    case CSSPropertyID::kVisibility: builder.SetVisibility(source.Visibility()); break;
    case CSSPropertyID::kWhiteSpaceCollapse: builder.SetWhiteSpaceCollapse(source.GetWhiteSpaceCollapse()); break;
    case CSSPropertyID::kLineHeight: builder.SetLineHeight(source.LineHeight()); break;
    case CSSPropertyID::kTabSize: builder.SetTabSize(source.GetTabSize()); break;
    case CSSPropertyID::kLetterSpacing:
    case CSSPropertyID::kWordSpacing: {
      auto description = builder.GetFont()->GetFontDescription();
      if (id == CSSPropertyID::kLetterSpacing) description.SetLetterSpacing(source.ComputedLetterSpacing());
      else description.SetWordSpacing(source.ComputedWordSpacing());
      builder.SetFont(WithDescription(*builder.GetFont(), description));
      break;
    }
    default: assert(false); break;
  }
}

void Apply(CSSPropertyID id, const CSSValue& value, const StyleResolverContext& context, ComputedStyleBuilder& builder) {
  const auto& parent = context.parent_style ? *context.parent_style : context.initial_style;
  if (value.IsCSSWideKeyword()) {
    const bool inherit = value.IsInheritedValue() ||
                         (value.IsUnsetValue() && GetCSSPropertyMetadata(id)->inherited);
    CopyProperty(id, inherit ? parent : context.initial_style, builder);
    if (inherit && !GetCSSPropertyMetadata(id)->inherited) builder.SetHasExplicitInheritance(true);
    return;
  }
  const auto* literal = DynamicTo<CSSNumericLiteralValue>(value);
  const auto* keyword = DynamicTo<CSSIdentifierValue>(value);
  switch (id) {
    case CSSPropertyID::kColor:
      builder.SetColor(keyword ? parent.Color() : ToColor4f(To<cssvalue::CSSColor>(value).Value()));
      break;
    case CSSPropertyID::kWebkitTextFillColor:
      builder.SetTextFillColor(keyword ? StyleColorValue::CurrentColor()
                                       : StyleColorValue(To<cssvalue::CSSColor>(value).Value()));
      break;
    case CSSPropertyID::kVisibility:
      builder.SetVisibility(keyword->GetValueID() == CSSValueID::kVisible  ? EVisibility::kVisible
                            : keyword->GetValueID() == CSSValueID::kHidden ? EVisibility::kHidden
                                                                           : EVisibility::kCollapse);
      break;
    case CSSPropertyID::kWhiteSpaceCollapse:
      switch (keyword->GetValueID()) {
        case CSSValueID::kPreserve: builder.SetWhiteSpaceCollapse(WhiteSpaceCollapse::kPreserve); break;
        case CSSValueID::kPreserveBreaks: builder.SetWhiteSpaceCollapse(WhiteSpaceCollapse::kPreserveBreaks); break;
        case CSSValueID::kBreakSpaces: builder.SetWhiteSpaceCollapse(WhiteSpaceCollapse::kBreakSpaces); break;
        default: builder.SetWhiteSpaceCollapse(WhiteSpaceCollapse::kCollapse); break;
      }
      break;
    case CSSPropertyID::kLineHeight:
      if (keyword) builder.SetLineHeight(Length::Auto());
      else if (literal && literal->IsNumber())
        builder.SetLineHeight(Length::Percent(ClampFloat(static_cast<long double>(literal->DoubleValue()) * 100)));
      else if (literal && literal->IsPercentage()) {
        // Match ConvertLineHeight's integer percentage conversion, including
        // its fractional-percentage truncation in the pinned upstream.
        const int percent = ClampTo<int>(literal->DoubleValue());
        builder.SetLineHeight(Length::Fixed(ClampFloat(
            static_cast<long double>(builder.GetFont()->GetFontDescription().ComputedSize()) * percent / 100)));
      } else builder.SetLineHeight(Length::Fixed(std::max(0.0f, ConvertLength(value, context, builder, true))));
      break;
    case CSSPropertyID::kTabSize:
      if (literal && literal->IsNumber()) builder.SetTabSize(TabSize(ClampFloat(literal->DoubleValue())));
      else builder.SetTabSize(TabSize(std::max(0.0f, ConvertLength(value, context, builder, false)), TabSizeValueType::kLength));
      break;
    case CSSPropertyID::kLetterSpacing:
    case CSSPropertyID::kWordSpacing: {
      const Length length = Length::Fixed(keyword ? 0.0f : ConvertLength(value, context, builder, false));
      auto description = builder.GetFont()->GetFontDescription();
      if (id == CSSPropertyID::kLetterSpacing) description.SetLetterSpacing(length);
      else description.SetWordSpacing(length);
      builder.SetFont(WithDescription(*builder.GetFont(), description));
      break;
    }
    default: assert(false); break;
  }
}

} // namespace

bool StyleResolverContext::IsValid() const {
  return ValidZoom(device_scale_factor, page_zoom_factor) && initial_style.EffectiveZoom() == EffectiveZoom() &&
         (!parent_style || parent_style->EffectiveZoom() == EffectiveZoom()) &&
         (!root_style || root_style->EffectiveZoom() == EffectiveZoom());
}
std::shared_ptr<const ComputedStyle> StyleResolver::CreateInitialStyle(const StyleResolverSettings& settings, float device, float page) {
  if (!ValidZoom(device, page)) return nullptr;
  // CSS initial spacing is independent of the host FontDescription. Legacy
  // spacing supplied in InlineStyle is overlaid separately as declarations.
  auto description = settings.default_font.GetFontDescription();
  description.SetLetterSpacing(Length::Fixed());
  description.SetWordSpacing(Length::Fixed());
  InlineStyle seed(WithDescription(settings.default_font, description));
  ComputedStyleBuilder builder(seed.Zoom(device * page).font, device * page);
  builder.SetColor(settings.initial_color);
  return builder.Build();
}
std::shared_ptr<const ComputedStyle> StyleResolver::Resolve(
    const StyleResolverContext& context, std::span<const StyleDeclaration* const> blocks, const InlineStyle* legacy) {
  if (!context.IsValid()) return nullptr;
  ComputedStyleBuilder builder(context.initial_style,
                               context.parent_style ? *context.parent_style : context.initial_style);
  if (legacy) {
    const InlineStyle zoomed = legacy->Zoom(context.EffectiveZoom());
    builder.SetFont(zoomed.font);
    builder.SetLineHeight(zoomed.line_height);
    builder.SetTabSize(zoomed.tab_size);
    builder.SetLegacyPaint(zoomed.paint);
    builder.SetLegacyBaselineShift(zoomed.baseline_shift);
    // The pre-declaration API documented preserved input whitespace with
    // break-spaces wrapping; declarations below may still override it.
    builder.SetWhiteSpaceCollapse(WhiteSpaceCollapse::kBreakSpaces);
  }
  std::array<const CSSValue*, static_cast<size_t>(CSSPropertyID::kCount)> winners{};
  for (const auto* block : blocks) {
    if (!block) continue;
    for (const auto& entry : block->Entries()) winners[static_cast<size_t>(entry.property)] = entry.value.get();
  }
  const auto apply = [&](CSSPropertyID id) {
    if (const auto* value = winners[static_cast<size_t>(id)]) Apply(id, *value, context, builder);
  };
  // Cascade-affecting properties and FontBuilder will be added before this
  // boundary when those longhands are ported. Do not fold these phases into
  // declaration-order application: lh units depend on the line-height phase.
  for (const auto& property : CSSProperties()) if (property.priority) apply(property.id);
  apply(CSSPropertyID::kLineHeight);
  for (const auto& property : CSSProperties())
    if (!property.priority && property.id != CSSPropertyID::kLineHeight) apply(property.id);
  return builder.Build();
}

} // namespace bkfont
