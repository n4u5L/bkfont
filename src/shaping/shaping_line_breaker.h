// Ported from: blink/renderer/platform/fonts/shaping/shaping_line_breaker.h
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <optional>

#include "shaping/run_segmenter.h"
#include "shaping/shape_options.h"
#include "shaping/shape_result_view.h"
#include "shaping/text_spacing_trim.h"
#include "shaping/support/layout_unit.h"
#include "platform_export.h"
#include "text/text_direction.h"
#include "base/text/atomic_string.h"

namespace bkfont {

class Font;
class ShapeResult;
class Hyphenation;
class LazyLineBreakIterator;

// Shapes a line of text by finding the ideal break position as indicated by the
// available space and the shape results for the entire paragraph. Once an ideal
// break position has been found the text is scanned backwards until a valid and
// and appropriate break opportunity is identified. Unless the break opportunity
// is at a safe-to-break boundary (as identified by HarfBuzz) the beginning and/
// or end of the line is reshaped to account for differences caused by breaking.
//
// This allows for significantly faster and more efficient line breaking by only
// reshaping when absolutely necessarily and by only evaluating likely candidate
// break opportunities instead of measuring and evaluating all possible options.
class PLATFORM_EXPORT ShapingLineBreaker {

public:
  // The paragraph result, iterator, optional hyphenation provider, and font
  // must outlive this breaker. Returned line views retain their glyph runs.
  ShapingLineBreaker(const ShapeResult* result,
                     const LazyLineBreakIterator* break_iterator,
                     const Hyphenation* hyphenation,
                     const Font* font);

  // Represents details of the result of |ShapeLine()|.
  struct Result {

  public:
    // Indicates the resulting break offset.
    unsigned break_offset;

    // Indicates that the shape result contains trailing spaces
    bool has_trailing_spaces;

    // True if there were no break opportunities that can fit. When this is
    // false, the result width should be smaller than or equal to the available
    // space.
    bool is_overflow;

    // True if the break is hyphenated, either by automatic hyphenation or
    // soft-hyphen characters.
    // The hyphen glyph is not included in the |ShapeResult|, and that appending
    // a hyphen glyph may overflow the specified available space.
    bool is_hyphenated;
  };

  // Set the start of the current line.
  void SetLineStart(unsigned offset) {
    line_start_ = offset;
  }
  // Disable reshaping the end edge if it is at a breakable space, even if it
  // is not safe-to-break. Good for performance if accurate width is not
  // critical.
  void SetDontReshapeEndIfAtSpace() {
    dont_reshape_end_if_at_space_ = true;
  }
  // Returns nullptr if this line overflows. When the word is very long, such
  // as URL or data, creating ShapeResult is expensive. Set this option to
  // suppress if ShapeResult is not needed when this line overflows.
  bool NoResultIfOverflow() const {
    return no_result_if_overflow_;
  }
  void SetNoResultIfOverflow() {
    no_result_if_overflow_ = true;
  }
  void SetIsAfterForcedBreak(bool value) {
    is_after_forced_break_ = value;
  }
  void SetTextSpacingTrim(TextSpacingTrim value) {
    text_spacing_trim_ = value;
  }

  std::unique_ptr<const ShapeResultView> ShapeLine(unsigned start_offset,
                                                   LayoutUnit available_space,
                                                   Result* result_out);

  std::unique_ptr<const ShapeResultView> ShapeLineAt(unsigned start, unsigned end);

protected:
  const ShapeResult& GetShapeResult() const {
    return *result_;
  }

  // Keep reshaped edges alive until their runs have been retained by the view.
  virtual std::shared_ptr<const ShapeResult> Shape(unsigned start,
                                                   unsigned end,
                                                   ShapeOptions = ShapeOptions()) = 0;

private:
  struct EdgeOffset {
    unsigned offset = 0;
    bool han_kerning = false;
  };

  const String& GetText() const;

  // True if the `offset` is start of a line, except the first line.
  bool IsStartOfWrappedLine(unsigned offset) const {
    return offset && offset == line_start_ && !is_after_forced_break_;
  }
  EdgeOffset FirstSafeOffset(unsigned start) const;

  // Represents a break opportunity offset and its properties.
  struct BreakOpportunity {

  public:
    BreakOpportunity() = default;
    BreakOpportunity(unsigned new_offset, bool hyphenated)
        : offset(new_offset),
          is_hyphenated(hyphenated) {
    }
    BreakOpportunity(unsigned new_offset, unsigned run_end, bool hyphenated)
        : offset(new_offset),
          non_hangable_run_end(run_end),
          is_hyphenated(hyphenated) {
    }

    unsigned offset = 0;
    std::optional<unsigned> non_hangable_run_end;
    bool is_hyphenated = false;
  };
  BreakOpportunity PreviousBreakOpportunity(unsigned offset,
                                            unsigned start) const;
  BreakOpportunity NextBreakOpportunity(unsigned offset,
                                        unsigned start,
                                        unsigned len) const;
  BreakOpportunity Hyphenate(unsigned offset,
                             unsigned start,
                             bool backwards) const;
  unsigned Hyphenate(unsigned offset,
                     unsigned word_start,
                     unsigned word_end,
                     bool backwards) const;

  std::unique_ptr<const ShapeResultView> ShapeToEnd(unsigned start,
                                                    const std::shared_ptr<const ShapeResult>& line_start_result,
                                                    unsigned first_safe,
                                                    unsigned range_start,
                                                    unsigned range_end);
  std::unique_ptr<const ShapeResultView> ConcatShapeResults(
      unsigned start,
      unsigned end,
      unsigned first_safe,
      unsigned last_safe,
      const std::shared_ptr<const ShapeResult>& line_start_result,
      const std::shared_ptr<const ShapeResult>& line_end_result);

  void SetBreakOffset(unsigned break_offset, const String&, Result*);
  void SetBreakOffset(const BreakOpportunity&, const String&, Result*);

  template <TextDirection>
  std::unique_ptr<const ShapeResultView> ShapeLine(unsigned start_offset,
                                                   LayoutUnit available_space,
                                                   Result* result_out);

  const ShapeResult* result_;
  const LazyLineBreakIterator* break_iterator_;
  const Hyphenation* hyphenation_;
  const Font* font_;
  unsigned line_start_ = 0;
  bool dont_reshape_end_if_at_space_ = false;
  bool no_result_if_overflow_ = false;
  bool is_after_forced_break_ = false;
  TextSpacingTrim text_spacing_trim_ = TextSpacingTrim::kInitial;

  friend class ShapingLineBreakerTest;
};

} // namespace bkfont
