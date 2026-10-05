// Local model adapter for Blink's LayoutText/LayoutInline. No DOM or GC.
#pragma once

#include <cstdint>
#include <memory>

#include "base/heap_vector.h"
#include "base/text/atomic_string.h"
#include "base/text/wtf_string.h"
#include "base/vector.h"
#include "font/font.h"
#include "geometry/length.h"
#include "layout/geometry/logical_size.h"
#include "paint/platform_paint.h"
#include "text/tab_size.h"
#include "style/computed_style.h"
#include "style/inline_style.h"
#include "style/style_declaration.h"

namespace bkfont {

class InlineFormattingContext;
using InlineNodeId = uint64_t;

// Subset of Blink's StyleChangeType.
enum class StyleChangeType {
  kNoStyleChange,
  kLocalStyleChange,
  kSubtreeStyleChange
};

class InlineObject final {
public:
  enum class Type {
    kInline,
    kText,
    kAtomic
  };

  InlineNodeId Id() const {
    return id_;
  }
  Type GetType() const {
    return type_;
  }
  bool IsText() const {
    return type_ == Type::kText;
  }
  bool IsInline() const {
    return type_ == Type::kInline;
  }
  bool IsAtomicInline() const {
    return type_ == Type::kAtomic;
  }
  bool IsAttached() const {
    return attached_;
  }
  InlineFormattingContext& Root() const {
    return *root_;
  }
  const InlineObject* Parent() const {
    return parent_;
  }
  const InlineObject* PreviousSibling() const;
  const InlineObject* NextSibling() const;
  const InlineObject* FirstChild() const;
  const InlineObject* LastChild() const;
  bool IsDescendantOf(const InlineObject&) const;
  const String& Text() const {
    return text_;
  }
  // The style from the last style recalc, as LayoutObject::Style(). Reading
  // it never recalculates; after style or tree mutations, call
  // InlineFormattingContext::UpdateStyle() (or ComputedStyleFor()) first.
  const ComputedStyle& Style() const;
  // Own a snapshot when retaining style across explicit update calls.
  std::shared_ptr<const ComputedStyle> StyleSnapshot() const { return computed_style_; }
  const StyleDeclaration& InlineDeclaration() const { return declaration_; }
  // Whether the object has style inputs of its own (legacy style, named rules
  // or declarations) instead of only inheriting.
  bool HasOwnStyle() const { return style_ || !rules_.empty() || !declaration_.IsEmpty(); }
  const Vector<AtomicString>& Rules() const { return rules_; }
  const std::shared_ptr<const InlineStyle>& SpecifiedStyle() const {
    return style_;
  }
  const std::shared_ptr<const ComputedStyle>& LayoutStyle() const;
  const HeapVector<std::unique_ptr<InlineObject>>& Children() const {
    return children_;
  }
  LogicalSize AtomicSize() const {
    return atomic_size_;
  }
  LayoutUnit AtomicBaseline() const {
    return atomic_baseline_;
  }
  LogicalSize LayoutAtomicSize() const;
  LayoutUnit LayoutAtomicBaseline() const;
  bool HasInlineFragments() const;

  InlineObject(const InlineObject&) = delete;
  InlineObject& operator=(const InlineObject&) = delete;

private:
  friend class InlineFormattingContext;
  InlineObject(InlineFormattingContext&, InlineNodeId, Type, std::shared_ptr<const InlineStyle>);
  InlineFormattingContext* root_;
  InlineNodeId id_;
  Type type_;
  InlineObject* parent_ = nullptr;
  bool attached_ = true;
  String text_;
  std::shared_ptr<const InlineStyle> style_;
  StyleDeclaration declaration_;
  Vector<AtomicString> rules_;
  std::shared_ptr<const ComputedStyle> computed_style_;
  // Node::NeedsStyleRecalc()/ChildNeedsStyleRecalc(). New objects need one.
  StyleChangeType style_change_ = StyleChangeType::kLocalStyleChange;
  bool child_needs_style_recalc_ = false;
  HeapVector<std::unique_ptr<InlineObject>> children_;
  LogicalSize atomic_size_;
  LayoutUnit atomic_baseline_;
};

} // namespace bkfont
