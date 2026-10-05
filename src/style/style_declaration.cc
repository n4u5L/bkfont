// Metadata/validation adapted from core/css/css_properties.json5 and the
// corresponding longhands in core/css/properties/longhands/longhands_custom.cc.
#include "style_declaration.h"

#include <algorithm>
#include <cmath>

#include "base/memory/values_equivalent.h"
#include "style/css_color.h"
#include "style/css_identifier_value.h"
#include "style/css_inherited_value.h"
#include "style/css_initial_value.h"
#include "style/css_numeric_literal_value.h"
#include "style/css_unset_value.h"

namespace bkfont {
namespace {

using UnitType = CSSPrimitiveValue::UnitType;

constexpr CSSPropertyMetadata kProperties[] = {
    {CSSPropertyID::kColor, "color", true, 1},
    {CSSPropertyID::kWebkitTextFillColor, "-webkit-text-fill-color", true, 0},
    {CSSPropertyID::kVisibility, "visibility", true, 0},
    {CSSPropertyID::kLineHeight, "line-height", true, 0},
    {CSSPropertyID::kLetterSpacing, "letter-spacing", true, 0},
    {CSSPropertyID::kWordSpacing, "word-spacing", true, 0},
    {CSSPropertyID::kTabSize, "tab-size", true, 0},
    {CSSPropertyID::kWhiteSpaceCollapse, "white-space-collapse", true, 0},
};
static_assert(std::size(kProperties) + 1 == static_cast<size_t>(CSSPropertyID::kCount));

// The ranges and units the longhands' ParseSingleValue() accepts.
bool IsValid(CSSPropertyID id, const CSSValue& value) {
  if (!GetCSSPropertyMetadata(id)) return false;
  if (value.IsCSSWideKeyword()) return true;
  const auto* keyword = DynamicTo<CSSIdentifierValue>(value);
  if (id == CSSPropertyID::kColor || id == CSSPropertyID::kWebkitTextFillColor) {
    if (keyword) return keyword->GetValueID() == CSSValueID::kCurrentcolor;
    if (const auto* color = DynamicTo<cssvalue::CSSColor>(value)) {
      const Color rgba = color->Value();
      return std::isfinite(rgba.Param0()) && std::isfinite(rgba.Param1()) && std::isfinite(rgba.Param2()) &&
             std::isfinite(rgba.Alpha());
    }
    return false;
  }
  if (id == CSSPropertyID::kWhiteSpaceCollapse) {
    return keyword && (keyword->GetValueID() == CSSValueID::kCollapse || keyword->GetValueID() == CSSValueID::kPreserve ||
                       keyword->GetValueID() == CSSValueID::kPreserveBreaks ||
                       keyword->GetValueID() == CSSValueID::kBreakSpaces);
  }
  if (id == CSSPropertyID::kVisibility) {
    return keyword && (keyword->GetValueID() == CSSValueID::kVisible || keyword->GetValueID() == CSSValueID::kHidden ||
                       keyword->GetValueID() == CSSValueID::kCollapse);
  }
  if (keyword) return id != CSSPropertyID::kTabSize && keyword->GetValueID() == CSSValueID::kNormal;
  const bool nonnegative = id == CSSPropertyID::kLineHeight || id == CSSPropertyID::kTabSize;
  if (const auto* literal = DynamicTo<CSSNumericLiteralValue>(value)) {
    if (!std::isfinite(literal->DoubleValue()) || (nonnegative && literal->DoubleValue() < 0)) return false;
    if (literal->IsLength()) return true;
    if (literal->IsNumber()) return nonnegative;
    // Percentage spacing needs the upstream runtime feature and font-relative
    // Length representation. Reject it until that path is ported.
    return literal->IsPercentage() && id == CSSPropertyID::kLineHeight;
  }
  if (const auto* calc = DynamicTo<CSSMathFunctionValue>(value)) {
    if (calc->Terms().empty()) return false;
    for (const auto& term : calc->Terms()) {
      if (!std::isfinite(term.value) || (!CSSPrimitiveValue::IsLength(term.unit) &&
          !(term.unit == UnitType::kPercentage && id == CSSPropertyID::kLineHeight))) return false;
    }
    // Negative calc terms are legal. Clamp a nonnegative property at computed
    // value time, after relative units and percentages have been resolved.
    return true;
  }
  return false;
}

std::shared_ptr<const CSSValue> CreateWideKeywordValue(CSSWideKeyword keyword) {
  switch (keyword) {
    case CSSWideKeyword::kInitial: return CSSInitialValue::Create();
    case CSSWideKeyword::kInherit: return CSSInheritedValue::Create();
    case CSSWideKeyword::kUnset: return cssvalue::CSSUnsetValue::Create();
  }
  return nullptr;
}

} // namespace

std::span<const CSSPropertyMetadata> CSSProperties() { return kProperties; }
const CSSPropertyMetadata* GetCSSPropertyMetadata(CSSPropertyID id) {
  const auto index = static_cast<size_t>(id);
  return index > 0 && index < static_cast<size_t>(CSSPropertyID::kCount) ? &kProperties[index - 1] : nullptr;
}

CSSLineHeight CSSLineHeight::Normal() {
  return CSSLineHeight(CSSIdentifierValue::Create(CSSValueID::kNormal));
}
CSSLineHeight CSSLineHeight::Number(double value) {
  return CSSLineHeight(CSSNumericLiteralValue::Create(value, UnitType::kNumber));
}
CSSLineHeight::CSSLineHeight(CSSLength value)
    : value_(CSSNumericLiteralValue::Create(value.value, value.unit)) {}
CSSLineHeight::CSSLineHeight(CSSCalc value)
    : value_(CSSMathFunctionValue::Create(std::move(value.terms))) {}

bool StyleDeclaration::Entry::operator==(const Entry& other) const {
  return property == other.property && base::ValuesEquivalent(value, other.value);
}

std::span<const StyleDeclaration::Entry> StyleDeclaration::Entries() const {
  return entries_ ? std::span<const Entry>(entries_->data(), entries_->size()) : std::span<const Entry>();
}
StyleDeclaration::EntryVector& StyleDeclaration::Access() {
  if (!entries_) entries_ = std::make_shared<EntryVector>();
  else if (entries_.use_count() != 1) entries_ = std::make_shared<EntryVector>(*entries_);
  return *entries_;
}
int StyleDeclaration::FindPropertyIndex(CSSPropertyID id) const {
  const auto entries = Entries();
  for (size_t i = 0; i < entries.size(); ++i)
    if (entries[i].property == id) return static_cast<int>(i);
  return -1;
}
const CSSValue* StyleDeclaration::Get(CSSPropertyID id) const {
  const int index = FindPropertyIndex(id);
  return index < 0 ? nullptr : Entries()[index].value.get();
}
bool StyleDeclaration::Set(CSSPropertyID id, std::shared_ptr<const CSSValue> value) {
  if (!value || !IsValid(id, *value)) return false;
  const int index = FindPropertyIndex(id);
  auto& entries = Access();
  if (index >= 0) entries.EraseAt(static_cast<wtf_size_t>(index));
  entries.push_back(Entry{id, std::move(value)});
  return true;
}
bool StyleDeclaration::SetCSSWideKeyword(CSSPropertyID id, CSSWideKeyword keyword) {
  return Set(id, CreateWideKeywordValue(keyword));
}
bool StyleDeclaration::Remove(CSSPropertyID id) {
  const int index = FindPropertyIndex(id);
  if (index < 0) return false;
  Access().EraseAt(static_cast<wtf_size_t>(index));
  return true;
}
void StyleDeclaration::Merge(const StyleDeclaration& other) {
  const StyleDeclaration snapshot(other);
  for (const auto& entry : snapshot.Entries()) (void)Set(entry.property, entry.value);
}
bool StyleDeclaration::operator==(const StyleDeclaration& other) const {
  return entries_ == other.entries_ || std::ranges::equal(Entries(), other.Entries());
}

bool StyleDeclaration::SetColor(StyleColorValue value) {
  return Set(CSSPropertyID::kColor, value.ToCSSValue());
}
bool StyleDeclaration::SetTextFillColor(StyleColorValue value) {
  return Set(CSSPropertyID::kWebkitTextFillColor, value.ToCSSValue());
}
bool StyleDeclaration::SetVisibility(EVisibility value) {
  switch (value) {
    case EVisibility::kVisible: return Set(CSSPropertyID::kVisibility, CSSIdentifierValue::Create(CSSValueID::kVisible));
    case EVisibility::kHidden: return Set(CSSPropertyID::kVisibility, CSSIdentifierValue::Create(CSSValueID::kHidden));
    case EVisibility::kCollapse: return Set(CSSPropertyID::kVisibility, CSSIdentifierValue::Create(CSSValueID::kCollapse));
  }
  return false;
}
bool StyleDeclaration::SetLineHeight(const CSSLineHeight& value) {
  return Set(CSSPropertyID::kLineHeight, value.Value());
}
bool StyleDeclaration::SetLetterSpacing(CSSLength value) {
  return Set(CSSPropertyID::kLetterSpacing, CSSNumericLiteralValue::Create(value.value, value.unit));
}
bool StyleDeclaration::SetLetterSpacing(CSSCalc value) {
  return Set(CSSPropertyID::kLetterSpacing, CSSMathFunctionValue::Create(std::move(value.terms)));
}
bool StyleDeclaration::SetLetterSpacingNormal() {
  return Set(CSSPropertyID::kLetterSpacing, CSSIdentifierValue::Create(CSSValueID::kNormal));
}
bool StyleDeclaration::SetWordSpacing(CSSLength value) {
  return Set(CSSPropertyID::kWordSpacing, CSSNumericLiteralValue::Create(value.value, value.unit));
}
bool StyleDeclaration::SetWordSpacing(CSSCalc value) {
  return Set(CSSPropertyID::kWordSpacing, CSSMathFunctionValue::Create(std::move(value.terms)));
}
bool StyleDeclaration::SetWordSpacingNormal() {
  return Set(CSSPropertyID::kWordSpacing, CSSIdentifierValue::Create(CSSValueID::kNormal));
}
bool StyleDeclaration::SetTabSize(double spaces) {
  return Set(CSSPropertyID::kTabSize, CSSNumericLiteralValue::Create(spaces, UnitType::kNumber));
}
bool StyleDeclaration::SetTabSize(CSSLength value) {
  return Set(CSSPropertyID::kTabSize, CSSNumericLiteralValue::Create(value.value, value.unit));
}
bool StyleDeclaration::SetWhiteSpaceCollapse(WhiteSpaceCollapse value) {
  switch (value) {
    case WhiteSpaceCollapse::kCollapse:
      return Set(CSSPropertyID::kWhiteSpaceCollapse, CSSIdentifierValue::Create(CSSValueID::kCollapse));
    case WhiteSpaceCollapse::kPreserve:
      return Set(CSSPropertyID::kWhiteSpaceCollapse, CSSIdentifierValue::Create(CSSValueID::kPreserve));
    case WhiteSpaceCollapse::kPreserveBreaks:
      return Set(CSSPropertyID::kWhiteSpaceCollapse, CSSIdentifierValue::Create(CSSValueID::kPreserveBreaks));
    case WhiteSpaceCollapse::kBreakSpaces:
      return Set(CSSPropertyID::kWhiteSpaceCollapse, CSSIdentifierValue::Create(CSSValueID::kBreakSpaces));
  }
  return false;
}
bool StyleDeclaration::SetTabSize(CSSCalc value) {
  return Set(CSSPropertyID::kTabSize, CSSMathFunctionValue::Create(std::move(value.terms)));
}

} // namespace bkfont
