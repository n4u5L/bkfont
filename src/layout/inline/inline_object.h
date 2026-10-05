// Local model adapter for Blink's LayoutText/LayoutInline. No DOM or GC.
#pragma once

#include <cstdint>
#include <memory>

#include "base/heap_vector.h"
#include "base/text/wtf_string.h"
#include "font/font.h"
#include "geometry/length.h"
#include "layout/geometry/logical_size.h"
#include "paint/platform_paint.h"
#include "text/tab_size.h"

namespace bkfont {

class InlineFormattingContext;
using InlineNodeId = uint64_t;

// Values supplied by the host before page/DSF zoom, without CSS parsing/cascading.
// A null object style inherits its parent's complete style.
// The host resolves FontDescription::Orientation for the IFC writing mode.
struct InlineStyle {
  explicit InlineStyle(Font font)
      : font(std::move(font)) {
  }
  Font font;
  Length line_height = Length::Auto();
  PlatformPaint paint;
  TabSize tab_size{8};
  // Positive shifts raise the baseline, in line-relative coordinates.
  LayoutUnit baseline_shift;
  // Style resolution boundary: fixed lengths and computed font sizes are
  // zoomed before shaping/layout. Percentages and specified font size remain
  // unchanged, including the specified size used for optical sizing.
  InlineStyle Zoom(float factor) const;
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
  const InlineStyle& Style() const;
  const std::shared_ptr<const InlineStyle>& SpecifiedStyle() const {
    return style_;
  }
  const std::shared_ptr<const InlineStyle>& LayoutStyle() const;
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
  friend class InlineEditor;
  InlineObject(InlineFormattingContext&, InlineNodeId, Type, std::shared_ptr<const InlineStyle>);
  InlineFormattingContext* root_;
  InlineNodeId id_;
  Type type_;
  InlineObject* parent_ = nullptr;
  bool attached_ = true;
  String text_;
  std::shared_ptr<const InlineStyle> style_;
  mutable std::shared_ptr<const InlineStyle> layout_style_;
  mutable std::shared_ptr<const InlineStyle> layout_style_source_;
  mutable float layout_style_zoom_ = 1;
  HeapVector<std::unique_ptr<InlineObject>> children_;
  LogicalSize atomic_size_;
  LayoutUnit atomic_baseline_;
};

} // namespace bkfont
