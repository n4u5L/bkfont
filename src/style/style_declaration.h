// MutableCSSPropertyValueSet equivalent for typed, ordered declarations.
#pragma once

#include <memory>
#include <span>
#include <string_view>
#include <utility>

#include "base/heap_vector.h"
#include "base/vector.h"
#include "style/computed_style_base_constants.h"
#include "style/css_math_function_value.h"
#include "style/css_primitive_value.h"
#include "style/css_value.h"
#include "style/style_color.h"
#include "style/white_space.h"

namespace bkfont {

// Only implemented properties are exposed. Extend the metadata and resolver
// together when porting another longhand; unknown IDs fail validation.
enum class CSSPropertyID {
  kInvalid,
  kColor,
  kWebkitTextFillColor,
  kVisibility,
  kLineHeight,
  kLetterSpacing,
  kWordSpacing,
  kTabSize,
  kWhiteSpaceCollapse,
  kCount
};

struct CSSPropertyMetadata {
  CSSPropertyID id;
  std::string_view name;
  bool inherited;
  unsigned priority;
};

std::span<const CSSPropertyMetadata> CSSProperties();
const CSSPropertyMetadata* GetCSSPropertyMetadata(CSSPropertyID);

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
  const std::shared_ptr<const CSSValue>& Value() const {
    return value_;
  }

private:
  explicit CSSLineHeight(std::shared_ptr<const CSSValue> value)
      : value_(std::move(value)) {
  }
  std::shared_ptr<const CSSValue> value_;
};

class StyleDeclaration {
public:
  // CSSPropertyValue: the value is shared and immutable.
  struct Entry {
    CSSPropertyID property;
    std::shared_ptr<const CSSValue> value;
    bool operator==(const Entry&) const;
  };

  std::span<const Entry> Entries() const;
  const CSSValue* Get(CSSPropertyID) const;
  bool IsEmpty() const {
    return Entries().empty();
  }
  // Returns false for null, invalid or unsupported input without changing the
  // block. A successful set moves this property to the end, including equal
  // values. Keep this order when persisting declarations or implementing undo.
  [[nodiscard]] bool Set(CSSPropertyID, std::shared_ptr<const CSSValue>);
  [[nodiscard]] bool SetCSSWideKeyword(CSSPropertyID, CSSWideKeyword);
  bool Remove(CSSPropertyID);
  void Merge(const StyleDeclaration&);
  bool operator==(const StyleDeclaration&) const;

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

private:
  // MutableCSSPropertyValueSet's property_vector_: at most one entry per
  // property, searched linearly. Copies share it until written.
  using EntryVector = HeapVector<Entry, 4>;
  int FindPropertyIndex(CSSPropertyID) const;
  EntryVector& Access();
  std::shared_ptr<EntryVector> entries_;
};

} // namespace bkfont
