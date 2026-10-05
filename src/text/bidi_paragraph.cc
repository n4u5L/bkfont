// Ported from: blink/renderer/platform/text/bidi_paragraph.cc
// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bidi_paragraph.h"
#include <utility>
#include <array>

#include "icu_error.h"
#include "base/text/character_names.h"
#include "base/text/string_builder.h"
#include "base/text/wtf_string.h"
#include "base/notreached.h"

namespace bkfont {

bool BidiParagraph::SetParagraph(const String& text,
                                 std::optional<TextDirection> base_direction) {

  if (!ubidi_) {
    ubidi_ = UBidiPtr(ubidi_open());
  }

  UBiDiLevel para_level;
  if (base_direction) {
    base_direction_ = *base_direction;
    para_level = IsLtr(base_direction_) ? UBIDI_LTR : UBIDI_RTL;
  } else {
    para_level = UBIDI_DEFAULT_LTR;
  }

  ICUError error;
  ubidi_setPara(ubidi_.get(), text.Span16().data(), text.length(), para_level, nullptr, &error);
  if (U_FAILURE(error)) {
    NOTREACHED();
  }

  if (!base_direction) {
    base_direction_ = DirectionFromLevel(ubidi_getParaLevel(ubidi_.get()));
  }

  return true;
}

// static
template <>
std::optional<TextDirection> BidiParagraph::BaseDirectionForString(
    base::span<const LChar> text,
    bool (*stop_at)(UChar)) {
  for (const LChar ch : text) {
    if (u_charDirection(ch) == U_LEFT_TO_RIGHT) {
      return TextDirection::kLtr;
    }

    if (stop_at && stop_at(ch)) {
      break;
    }
  }
  return std::nullopt;
}

// static
template <>
std::optional<TextDirection> BidiParagraph::BaseDirectionForString(
    base::span<const UChar> text,
    bool (*stop_at)(UChar)) {
  const UChar* data = text.data();
  const size_t len = text.size();
  for (size_t i = 0; i < len;) {
    UChar32 ch;
    UNSAFE_TODO(U16_NEXT(data, i, len, ch));
    switch (u_charDirection(ch)) {
    case U_LEFT_TO_RIGHT:
      return TextDirection::kLtr;
    case U_RIGHT_TO_LEFT:
    case U_RIGHT_TO_LEFT_ARABIC:
      return TextDirection::kRtl;
    default:
      break;
    }

    if (stop_at && stop_at(ch)) {
      break;
    }
  }
  return std::nullopt;
}

// static
std::optional<TextDirection> BidiParagraph::BaseDirectionForString(
    const StringView& text,
    bool (*stop_at)(UChar)) {
  return text.Is8Bit() ? BaseDirectionForString(text.Span8(), stop_at)
                       : BaseDirectionForString(text.Span16(), stop_at);
}

// static
String BidiParagraph::StringWithDirectionalOverride(const StringView& text,
                                                    TextDirection direction) {
  StringBuilder builder;
  builder.Reserve16BitCapacity(text.length() + 2);
  builder.Append(IsLtr(direction) ? uchar::kLeftToRightOverride
                                  : uchar::kRightToLeftOverride);
  builder.Append(text);
  builder.Append(uchar::kPopDirectionalFormatting);
  return builder.ToString();
}

unsigned BidiParagraph::GetLogicalRun(unsigned start, UBiDiLevel* level) const {
  int32_t end;
  ubidi_getLogicalRun(ubidi_.get(), start, &end, level);
  return end;
}

void BidiParagraph::GetLogicalRuns(const String& text, Runs* runs) const {

  for (unsigned start = 0; start < text.length();) {
    UBiDiLevel level;
    unsigned end = GetLogicalRun(start, &level);

    runs->emplace_back(start, end, level);
    start = end;
  }
}

void BidiParagraph::GetVisualRuns(const String& text, Runs* runs) const {

  Runs logical_runs;
  GetLogicalRuns(text, &logical_runs);

  const size_t run_count = logical_runs.size();
  std::array<UBiDiLevel, 32> stack_levels{};
  auto heap_levels = run_count > stack_levels.size()
                         ? std::make_unique<UBiDiLevel[]>(run_count)
                         : nullptr;
  base::span<UBiDiLevel> levels(
      heap_levels ? heap_levels.get() : stack_levels.data(),
      run_count);
  size_t i = 0;
  for (const Run& run : logical_runs) {
    levels[i++] = run.level;
  }
  std::array<int32_t, 32> stack_indices{};
  auto heap_indices = run_count > stack_indices.size()
                          ? std::make_unique<int32_t[]>(run_count)
                          : nullptr;
  base::span<int32_t> indices_in_visual_order(
      heap_indices ? heap_indices.get() : stack_indices.data(),
      run_count);
  IndicesInVisualOrder(levels, indices_in_visual_order);

  for (int32_t index : indices_in_visual_order) {
    runs->push_back(logical_runs[index]);
  }
}

// static
void BidiParagraph::IndicesInVisualOrder(
    base::span<const UBiDiLevel> levels,
    base::span<int32_t> indices_in_visual_order_out) {
  ubidi_reorderVisual(levels.data(), levels.size(), indices_in_visual_order_out.data());
}

} // namespace bkfont
