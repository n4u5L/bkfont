#include "computed_style.h"

namespace bkfont {
namespace {

bool PaintEqual(const PlatformPaint& a, const PlatformPaint& b) {
  return a.GetColor4f() == b.GetColor4f() && a.GetShader() == b.GetShader() &&
         a.GetBlendMode() == b.GetBlendMode() && a.IsAntiAlias() == b.IsAntiAlias();
}

} // namespace

bool ComputedStyle::InheritedData::operator==(const InheritedData& other) const {
  return line_height == other.line_height && tab_size == other.tab_size &&
         PaintEqual(paint, other.paint) && text_fill_color == other.text_fill_color &&
         visibility == other.visibility && white_space_collapse == other.white_space_collapse &&
         baseline_shift == other.baseline_shift;
}
bool ComputedStyle::operator==(const ComputedStyle& other) const {
  return InheritedEqual(other) &&
         (non_inherited_ == other.non_inherited_ || *non_inherited_ == *other.non_inherited_);
}
bool ComputedStyle::InheritedEqual(const ComputedStyle& other) const {
  return effective_zoom_ == other.effective_zoom_ &&
         (font_ == other.font_ || font_->font == other.font_->font) &&
         (inherited_ == other.inherited_ || *inherited_ == *other.inherited_);
}
PlatformPaint ComputedStyle::TextPaint() const {
  PlatformPaint paint = inherited_->paint;
  paint.SetColor(TextFillColor().Resolve(Color()));
  return paint;
}
StyleDifference ComputedStyle::VisualInvalidationDiff(const ComputedStyle& other) const {
  StyleDifference diff;
  // css_properties.json5: white-space-collapse invalidates "reshape".
  if ((font_ != other.font_ && font_->font != other.font_->font) ||
      GetWhiteSpaceCollapse() != other.GetWhiteSpaceCollapse())
    diff.SetNeedsReshape();
  if (effective_zoom_ != other.effective_zoom_ || LineHeight() != other.LineHeight() ||
      GetTabSize() != other.GetTabSize() || LegacyBaselineShift() != other.LegacyBaselineShift())
    diff.SetNeedsFullLayout();
  if (!PaintEqual(TextPaint(), other.TextPaint()) || Visibility() != other.Visibility())
    diff.SetNeedsNormalPaintInvalidation();
  // Keep Blink's visibility:collapse transition semantics even though the
  // current inline-only layout has no collapsing table rows or flex items.
  if ((Visibility() == EVisibility::kCollapse) != (other.Visibility() == EVisibility::kCollapse))
    diff.SetNeedsFullLayout();
  return diff;
}

ComputedStyleBuilder::ComputedStyleBuilder(const Font& font, float zoom)
    : font_(std::make_shared<ComputedStyle::FontData>(font)),
      inherited_(std::make_shared<ComputedStyle::InheritedData>()),
      non_inherited_(std::make_shared<ComputedStyle::NonInheritedData>()), effective_zoom_(zoom) {}
ComputedStyleBuilder::ComputedStyleBuilder(const ComputedStyle& style)
    : font_(style.font_), inherited_(style.inherited_), non_inherited_(style.non_inherited_),
      effective_zoom_(style.effective_zoom_) {}
ComputedStyleBuilder::ComputedStyleBuilder(const ComputedStyle& initial_style, const ComputedStyle& parent_style)
    : ComputedStyleBuilder(initial_style) {
  InheritFrom(parent_style);
}
void ComputedStyleBuilder::InheritFrom(const ComputedStyle& parent_style) {
  font_ = Group<ComputedStyle::FontData>(parent_style.font_);
  inherited_ = Group<ComputedStyle::InheritedData>(parent_style.inherited_);
}
void ComputedStyleBuilder::SetHasExplicitInheritance(bool value) {
  if (non_inherited_.Read().explicit_inheritance != value) non_inherited_.Access().explicit_inheritance = value;
}
void ComputedStyleBuilder::SetFont(const Font& value) {
  if (font_.Read().font != value) font_.Access().font = value;
}
void ComputedStyleBuilder::SetLineHeight(const Length& value) {
  if (inherited_.Read().line_height != value) inherited_.Access().line_height = value;
}
void ComputedStyleBuilder::SetTabSize(const TabSize& value) {
  if (inherited_.Read().tab_size != value) inherited_.Access().tab_size = value;
}
void ComputedStyleBuilder::SetColor(Color4f value) {
  if (inherited_.Read().paint.GetColor4f() != value) inherited_.Access().paint.SetColor(value);
}
void ComputedStyleBuilder::SetTextFillColor(StyleColorValue value) {
  if (inherited_.Read().text_fill_color != value) inherited_.Access().text_fill_color = value;
}
void ComputedStyleBuilder::SetVisibility(EVisibility value) {
  if (inherited_.Read().visibility != value) inherited_.Access().visibility = value;
}
void ComputedStyleBuilder::SetWhiteSpaceCollapse(WhiteSpaceCollapse value) {
  if (inherited_.Read().white_space_collapse != value) inherited_.Access().white_space_collapse = value;
}
void ComputedStyleBuilder::SetLegacyPaint(const PlatformPaint& value) {
  if (!PaintEqual(inherited_.Read().paint, value)) inherited_.Access().paint = value;
}
void ComputedStyleBuilder::SetLegacyBaselineShift(LayoutUnit value) {
  if (inherited_.Read().baseline_shift != value) inherited_.Access().baseline_shift = value;
}
std::shared_ptr<const ComputedStyle> ComputedStyleBuilder::Build() {
  return std::shared_ptr<const ComputedStyle>(
      new ComputedStyle(font_.Freeze(), inherited_.Freeze(), non_inherited_.Freeze(), effective_zoom_));
}

} // namespace bkfont
