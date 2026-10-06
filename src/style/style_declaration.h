// MutableCSSPropertyValueSet equivalent for typed, ordered declarations.
//
// This is the low-level style input: there is no CSS parser, so callers
// build the CSSValue objects the parser would produce (CSSIdentifierValue,
// CSSNumericLiteralValue, CSSMathFunctionValue, CSSValueList, ...) and Set()
// checks them against the grammar of the property, as the longhands'
// ParseSingleValue() would. Shorthands are expanded when set, as
// CSSPropertyParser does; the block stores only longhands.
#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "base/heap_vector.h"
#include "base/text/atomic_string.h"
#include "base/text/wtf_string.h"
#include "base/vector.h"
#include "style/computed_style_base_constants.h"
#include "style/css_math_function_value.h"
#include "style/css_primitive_value.h"
#include "style/css_property_names.h"
#include "style/css_value.h"
#include "style/css_value_keywords.h"
#include "style/style_color.h"
#include "style/white_space.h"

namespace bkfont {

// Typed setter inputs, in place of parsed text. They create the CSSValue
// subclasses the parser would: CSSInitialValue, CSSInheritedValue and
// CSSUnsetValue; CSSNumericLiteralValue; CSSMathFunctionValue.
enum class CSSWideKeyword {
  kInitial,
  kInherit,
  kUnset
};

struct CSSLength {
  double value;
  CSSPrimitiveValue::UnitType unit;
};

struct CSSCalc {
  Vector<CSSMathFunctionValue::Term> terms;
};

class CSSLineHeight {
public:
  static CSSLineHeight Normal();
  static CSSLineHeight Number(double);
  CSSLineHeight(CSSLength);
  CSSLineHeight(CSSCalc);
  const std::shared_ptr<const CSSValue>& Value() const { return value_; }

private:
  explicit CSSLineHeight(std::shared_ptr<const CSSValue> value) : value_(std::move(value)) {}
  std::shared_ptr<const CSSValue> value_;
};

// One <family-name> or <generic-family> of font-family. A generic family is
// a keyword (serif ... math); otherwise `name` is the family name.
struct CSSFontFamilyName {
  CSSValueID generic = CSSValueID::kInvalid;
  AtomicString name;
};

// One <shadow> of text-shadow. Omitted parts are null, as parsed.
struct CSSTextShadow {
  CSSLength x;
  CSSLength y;
  std::optional<CSSLength> blur;
  std::optional<StyleColorValue> color;
};

class StyleDeclaration {
public:
  // CSSPropertyValue: the value is shared and immutable.
  struct Entry {
    CSSPropertyID property;
    std::shared_ptr<const CSSValue> value;
    bool operator==(const Entry&) const;
  };

  // The property's grammar, as its ParseSingleValue() accepts it. Calc()
  // ranges are not checked: Set() gives a calc() the property's range.
  static bool IsValidValue(CSSPropertyID, const CSSValue&);

  std::span<const Entry> Entries() const;
  // Stored longhand values. Shorthands expand when set and have no entry.
  const CSSValue* Get(CSSPropertyID) const;
  bool IsEmpty() const { return Entries().empty(); }
  // Returns false for null, invalid or unsupported input without changing the
  // block. Aliases resolve to their property. Entries are kept in write order
  // with at most one per longhand (MutableCSSPropertyValueSet::
  // SetLonghandProperty): an existing entry is replaced in place, unless a
  // later entry is in the same logical property group with the other mapping
  // logic. Then the old entry is removed and the new one appended, so the
  // later of a logical/physical pair still wins in the cascade. A shorthand
  // takes a CSS-wide keyword (set on all its longhands) or, for white-space,
  // its single keywords; other shorthand values use SetShorthand().
  [[nodiscard]] bool Set(CSSPropertyID, std::shared_ptr<const CSSValue>);
  [[nodiscard]] bool SetCSSWideKeyword(CSSPropertyID, CSSWideKeyword);
  // Expands a shorthand from the values of some of its longhands, as its
  // ParseShorthand() does. Longhands which are not listed get the value the
  // parser gives an omitted component (the longhand's initial value, or the
  // explicit defaults of font and font-synthesis). Fails without changes if
  // a value is invalid, a required component (font-size and font-family of
  // font) is missing, or a longhand does not belong to the shorthand.
  [[nodiscard]] bool SetShorthand(CSSPropertyID shorthand, std::span<const Entry> longhands);
  bool Remove(CSSPropertyID);
  void Merge(const StyleDeclaration&);
  bool operator==(const StyleDeclaration&) const;

  // Generic typed helpers.
  [[nodiscard]] bool SetKeyword(CSSPropertyID, CSSValueID);
  // A space-separated list of keywords (font-variant-ligatures, ...).
  [[nodiscard]] bool SetKeywordList(CSSPropertyID, std::span<const CSSValueID>);
  [[nodiscard]] bool SetNumber(CSSPropertyID, double);
  [[nodiscard]] bool SetLength(CSSPropertyID, CSSLength);
  [[nodiscard]] bool SetCalc(CSSPropertyID, CSSCalc);
  [[nodiscard]] bool SetString(CSSPropertyID, const String&);

  // Property-specific typed setters.
  [[nodiscard]] bool SetColor(StyleColorValue);
  [[nodiscard]] bool SetTextFillColor(StyleColorValue);
  [[nodiscard]] bool SetVisibility(EVisibility);
  [[nodiscard]] bool SetLineHeight(const CSSLineHeight&);
  [[nodiscard]] bool SetLetterSpacing(CSSLength);
  [[nodiscard]] bool SetLetterSpacing(CSSCalc);
  [[nodiscard]] bool SetLetterSpacingNormal();
  [[nodiscard]] bool SetWordSpacing(CSSLength);
  [[nodiscard]] bool SetWordSpacing(CSSCalc);
  [[nodiscard]] bool SetWordSpacingNormal();
  [[nodiscard]] bool SetTabSize(double spaces);
  [[nodiscard]] bool SetTabSize(CSSLength);
  [[nodiscard]] bool SetTabSize(CSSCalc);
  [[nodiscard]] bool SetWhiteSpaceCollapse(WhiteSpaceCollapse);
  [[nodiscard]] bool SetTextWrapMode(TextWrapMode);
  [[nodiscard]] bool SetWhiteSpace(EWhiteSpace);
  // Typed equivalent of the two-keyword shorthand syntax.
  [[nodiscard]] bool SetWhiteSpace(WhiteSpaceCollapse, TextWrapMode);
  [[nodiscard]] bool SetFontFamily(std::span<const CSSFontFamilyName>);
  // <integer> values; 1 for a bare tag ("on").
  [[nodiscard]] bool SetFontFeatureSettings(std::span<const std::pair<AtomicString, int>>);
  [[nodiscard]] bool SetFontVariationSettings(std::span<const std::pair<AtomicString, double>>);
  // font-style: oblique <angle>. The number must lie in [-90, 90] in the
  // angle's own unit (IsAngleWithinLimits()). A zero angle is stored as
  // 'normal', as FontStyleObliqueZeroDegreeAsNormal parses it.
  [[nodiscard]] bool SetFontStyleOblique(CSSLength angle);
  // font-style: oblique calc(<angle>). A sum outside [-90deg, 90deg] is
  // stored as that limit; a zero sum stays a calc().
  [[nodiscard]] bool SetFontStyleOblique(CSSCalc angle);
  [[nodiscard]] bool SetTextShadow(std::span<const CSSTextShadow>);
  [[nodiscard]] bool SetTextDecorationLine(TextDecorationLine);
  [[nodiscard]] bool SetTextDecorationColor(StyleColorValue);
  [[nodiscard]] bool SetTextEmphasisColor(StyleColorValue);
  [[nodiscard]] bool SetTextStrokeColor(StyleColorValue);
  // -webkit-locale: a BCP 47 language tag, as the lang attribute maps it.
  [[nodiscard]] bool SetLocale(const String&);

private:
  // MutableCSSPropertyValueSet's property_vector_: at most one entry per
  // property, searched linearly. Copies share it until written.
  using EntryVector = HeapVector<Entry, 4>;
  bool SetLonghand(CSSPropertyID, std::shared_ptr<const CSSValue>);
  int FindPropertyIndex(CSSPropertyID) const;
  // MutableCSSPropertyValueSet::FindInsertionPointForID(): the index to
  // replace, or -1 to append. May remove the existing entry.
  int FindInsertionPointForID(CSSPropertyID);
  EntryVector& Access();
  std::shared_ptr<EntryVector> entries_;
};

} // namespace bkfont
