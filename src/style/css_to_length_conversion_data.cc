// Adapted from core/css/css_to_length_conversion_data.cc and
// css_length_resolver.cc.
#include "css_to_length_conversion_data.h"

#include <limits>

#include "base/math_extras.h"
#include "font/simple_font_data.h"
#include "style/computed_style.h"

namespace bkfont {
namespace {

// core/css/css_resolution_units.h
constexpr double kCssPixelsPerInch = 96;
constexpr double kCentimetersPerInch = 2.54;
constexpr double kMillimetersPerCentimeter = 10;
constexpr double kQuarterMillimetersPerCentimeter = 40;
constexpr double kPointsPerInch = 72;
constexpr double kPicasPerInch = 6;
constexpr double kCssPixelsPerCentimeter = kCssPixelsPerInch / kCentimetersPerInch;
constexpr double kCssPixelsPerMillimeter = kCssPixelsPerCentimeter / kMillimetersPerCentimeter;
constexpr double kCssPixelsPerQuarterMillimeter = kCssPixelsPerCentimeter / kQuarterMillimetersPerCentimeter;
constexpr double kCssPixelsPerPoint = kCssPixelsPerInch / kPointsPerInch;
constexpr double kCssPixelsPerPica = kCssPixelsPerInch / kPicasPerInch;

} // namespace

CSSToLengthConversionData::FontSizes::FontSizes(const FontSizeStyle& style, const ComputedStyle* root_style)
    : FontSizes(style.SpecifiedFontSize(),
                root_style ? root_style->SpecifiedFontSize() : style.SpecifiedFontSize(), style.GetFont(),
                root_style ? *root_style->GetFont() : style.GetFont(), style.EffectiveZoom(),
                root_style ? root_style->EffectiveZoom() : style.EffectiveZoom()) {}

float CSSToLengthConversionData::FontSizes::Ex(float zoom) const {
  const SimpleFontData* font_data = font_ ? font_->PrimaryFont() : nullptr;
  if (!font_data || !font_data->GetFontMetrics().HasXHeight()) return em_ / 2.0f;
  // Font-metrics-based units are pre-zoomed with a factor of `font_zoom_`,
  // we need to unzoom using that factor before applying the target zoom.
  return font_data->GetFontMetrics().XHeight() / font_zoom_ * zoom;
}

float CSSToLengthConversionData::FontSizes::Rex(float zoom) const {
  const SimpleFontData* font_data = root_font_ ? root_font_->PrimaryFont() : nullptr;
  if (!font_data || !font_data->GetFontMetrics().HasXHeight()) return rem_ / 2.0f;
  return font_data->GetFontMetrics().XHeight() / root_font_zoom_ * zoom;
}

float CSSToLengthConversionData::FontSizes::Ch(float zoom) const {
  const SimpleFontData* font_data = font_ ? font_->PrimaryFontWithDigitZero() : nullptr;
  if (!font_data) return 0;
  return font_data->GetFontMetrics().ZeroWidth() / font_zoom_ * zoom;
}

float CSSToLengthConversionData::FontSizes::Rch(float zoom) const {
  const SimpleFontData* font_data = root_font_ ? root_font_->PrimaryFontWithDigitZero() : nullptr;
  if (!font_data) return 0;
  return font_data->GetFontMetrics().ZeroWidth() / root_font_zoom_ * zoom;
}

float CSSToLengthConversionData::FontSizes::Ic(float zoom) const {
  const SimpleFontData* font_data = font_ ? font_->PrimaryFontWithCjkWater() : nullptr;
  std::optional<float> full_width;
  if (font_data) full_width = font_data->IdeographicInlineSize();
  if (!full_width.has_value()) return Em(zoom);
  return full_width.value() / font_zoom_ * zoom;
}

float CSSToLengthConversionData::FontSizes::Ric(float zoom) const {
  const SimpleFontData* font_data = root_font_ ? root_font_->PrimaryFontWithCjkWater() : nullptr;
  std::optional<float> full_width;
  if (font_data) full_width = font_data->IdeographicInlineSize();
  if (!full_width.has_value()) return Rem(zoom);
  return full_width.value() / root_font_zoom_ * zoom;
}

float CSSToLengthConversionData::FontSizes::Cap(float zoom) const {
  const SimpleFontData* font_data = font_ ? font_->PrimaryFont() : nullptr;
  if (!font_data) return 0.0f;
  return font_data->GetFontMetrics().CapHeight() / font_zoom_ * zoom;
}

float CSSToLengthConversionData::FontSizes::Rcap(float zoom) const {
  const SimpleFontData* font_data = root_font_ ? root_font_->PrimaryFont() : nullptr;
  if (!font_data) return 0.0f;
  return font_data->GetFontMetrics().CapHeight() / root_font_zoom_ * zoom;
}

CSSToLengthConversionData::LineHeightSize::LineHeightSize(const FontSizeStyle& style,
                                                          const ComputedStyle* root_style)
    : LineHeightSize(style.SpecifiedLineHeight(),
                     root_style ? root_style->SpecifiedLineHeight() : style.SpecifiedLineHeight(), style.GetFont(),
                     root_style ? *root_style->GetFont() : style.GetFont(), style.EffectiveZoom(),
                     root_style ? root_style->EffectiveZoom() : style.EffectiveZoom()) {}

float CSSToLengthConversionData::LineHeightSize::Lh(float zoom) const {
  if (!font_) return 0;
  // Like font-metrics-based units, lh is also based on pre-zoomed font
  // metrics. We therefore need to unzoom using the font zoom before applying
  // the target zoom.
  return ComputedStyle::ComputedLineHeight(line_height_, *font_) / font_zoom_ * zoom;
}

float CSSToLengthConversionData::LineHeightSize::Rlh(float zoom) const {
  if (!root_font_) return 0;
  return ComputedStyle::ComputedLineHeight(root_line_height_, *root_font_) / root_font_zoom_ * zoom;
}

CSSToLengthConversionData::CSSToLengthConversionData(const FontSizes& font_sizes,
                                                     const LineHeightSize& line_height_size, float zoom)
    : font_sizes_(font_sizes), line_height_size_(line_height_size),
      zoom_(ClampTo<float>(zoom, std::numeric_limits<float>::denorm_min())) {}

void CSSToLengthConversionData::SetZoom(float zoom) {
  zoom_ = ClampTo<float>(zoom, std::numeric_limits<float>::denorm_min());
}

double CSSToLengthConversionData::ZoomedComputedPixels(double value, CSSPrimitiveValue::UnitType type) const {
  using UnitType = CSSPrimitiveValue::UnitType;
  switch (type) {
    case UnitType::kPixels: return value * Zoom();
    case UnitType::kCentimeters: return value * kCssPixelsPerCentimeter * Zoom();
    case UnitType::kMillimeters: return value * kCssPixelsPerMillimeter * Zoom();
    case UnitType::kQuarterMillimeters: return value * kCssPixelsPerQuarterMillimeter * Zoom();
    case UnitType::kInches: return value * kCssPixelsPerInch * Zoom();
    case UnitType::kPoints: return value * kCssPixelsPerPoint * Zoom();
    case UnitType::kPicas: return value * kCssPixelsPerPica * Zoom();
    // Note that functions for font-relative units already account for the
    // zoom factor.
    case UnitType::kEms: return value * EmFontSize(Zoom());
    case UnitType::kExs: return value * ExFontSize(Zoom());
    case UnitType::kRexs: return value * RexFontSize(Zoom());
    case UnitType::kRems: return value * RemFontSize(Zoom());
    case UnitType::kChs: return value * ChFontSize(Zoom());
    case UnitType::kRchs: return value * RchFontSize(Zoom());
    case UnitType::kIcs: return value * IcFontSize(Zoom());
    case UnitType::kRics: return value * RicFontSize(Zoom());
    case UnitType::kLhs: return value * LineHeight(Zoom());
    case UnitType::kRlhs: return value * RootLineHeight(Zoom());
    case UnitType::kCaps: return value * CapFontSize(Zoom());
    case UnitType::kRcaps: return value * RcapFontSize(Zoom());
    default: return 0;
  }
}

} // namespace bkfont
