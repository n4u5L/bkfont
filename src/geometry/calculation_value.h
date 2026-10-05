// Ported from: blink/renderer/platform/geometry/calculation_value.h
/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <memory>
#include "base/types/pass_key.h"
#include "length.h"
#include "length_functions.h"
#include "base/allocator/allocator.h"

namespace bkfont {

class CalculationExpressionNode;

class CalculationValue {
public:
  CalculationValue(PixelsAndPercent value, Length::ValueRange range)
      : value_(value),
        is_non_negative_(range == Length::ValueRange::kNonNegative) {
  }

  using PassKey = base::PassKey<CalculationValue>;
  CalculationValue(PassKey,
                   std::shared_ptr<const CalculationExpressionNode> expression,
                   Length::ValueRange range);

  // If |expression| simply wraps a |PixelsAndPercent| value, this function
  // takes that value directly and discards |expression|.
  static std::shared_ptr<const CalculationValue> CreateSimplified(
      std::shared_ptr<const CalculationExpressionNode> expression,
      Length::ValueRange range);

  ~CalculationValue();

  float Evaluate(float max_value, const EvaluationInput& = {}) const;
  bool operator==(const CalculationValue& o) const;
  bool IsExpression() const {
    return static_cast<bool>(expression_);
  }
  bool IsNonNegative() const {
    return is_non_negative_;
  }
  Length::ValueRange GetValueRange() const {
    return is_non_negative_ ? Length::ValueRange::kNonNegative
                            : Length::ValueRange::kAll;
  }
  bool HasAuto() const;
  bool HasContentOrIntrinsicSize() const;
  bool HasAutoOrContentOrIntrinsicSize() const;
  bool HasPercent() const;
  bool HasPercentOrStretch() const;
  bool HasStretch() const;

  bool HasMinContent() const;
  bool HasMaxContent() const;
  bool HasFitContent() const;

  bool HasOnlyFixedAndPercent() const;

  float Pixels() const {
    ;
    return value_.pixels;
  }
  float Percent() const {
    ;
    return value_.percent;
  }
  PixelsAndPercent GetPixelsAndPercent() const {
    ;
    return value_;
  }
  bool HasExplicitPixels() const {
    ;
    return value_.has_explicit_pixels;
  }
  bool HasExplicitPercent() const {
    ;
    return value_.has_explicit_percent;
  }

  // If |this| is an expression, returns the underlying expression. Otherwise,
  // creates one from the underlying |PixelsAndPercent| value.
  std::shared_ptr<const CalculationExpressionNode> GetOrCreateExpression() const;

  std::shared_ptr<const CalculationValue> Blend(const CalculationValue& from,
                                                double progress,
                                                Length::ValueRange) const;
  std::shared_ptr<const CalculationValue> SubtractFromOneHundredPercent() const;
  std::shared_ptr<const CalculationValue> Add(const CalculationValue&) const;
  std::shared_ptr<const CalculationValue> Zoom(double factor) const;

private:
  // `value_` and `expression_` are mutually exclusive.
  PixelsAndPercent value_;
  std::shared_ptr<const CalculationExpressionNode> expression_ = nullptr;
  const bool is_non_negative_;
};

} // namespace bkfont
