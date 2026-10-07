// Ported from: blink/renderer/platform/geometry/calculation_expression_node.h
// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include "base/vector.h"
#include "length.h"
#include "base/casting.h"
#include "base/ref_counted.h"
#include "base/text/atomic_string.h"

namespace bkit {

enum class CalculationOperator {
  kAdd,
  kSubtract,
  kMultiply, // Division is converted to multiplication and use this value too.
  kInvert,
  kMin,
  kMax,
  kClamp,
  kRoundNearest,
  kRoundUp,
  kRoundDown,
  kRoundToZero,
  kMod,
  kRem,
  kLog,
  kExp,
  kSqrt,
  kHypot,
  kAbs,
  kSign,
  kProgress,
  kContainerProgress,
  kCalcSize,
  kMediaProgress,
  kPow,
  kSin,
  kCos,
  kTan,
  kAsin,
  kAcos,
  kAtan,
  kAtan2,
};

// Represents an expression composed of numbers, |PixelsAndPercent| and multiple
// types of operators. To be consumed by |Length| values that involve
// non-trivial math functions like min() and max().
class CalculationExpressionNode
    : public std::enable_shared_from_this<CalculationExpressionNode> {
public:
  virtual float Evaluate(float max_value, const EvaluationInput&) const = 0;
  bool operator==(const CalculationExpressionNode& other) const {
    return Equals(other);
  }
  bool operator!=(const CalculationExpressionNode& other) const {
    return !operator==(other);
  }

  bool HasAuto() const {
    return has_auto_;
  }
  bool HasContentOrIntrinsicSize() const {
    return has_content_or_intrinsic_;
  }
  bool HasAutoOrContentOrIntrinsicSize() const {
    return has_auto_ || has_content_or_intrinsic_;
  }
  bool HasStretch() const {
    return has_stretch_;
  }
  // HasPercent returns whether this node's value expression should be
  // treated as having a percent.  Note that this means that percentages
  // inside of the calculation part of a calc-size() do not make the
  // calc-size() act as though it has a percent.
  bool HasPercent() const {
    return has_percent_;
  }
  bool HasPercentOrStretch() const {
    return has_percent_ || has_stretch_;
  }
  bool HasColorChannelKeyword() const {
    return has_color_channel_keyword_;
  }

  virtual bool HasMinContent() const {
    return false;
  }
  virtual bool HasMaxContent() const {
    return false;
  }
  virtual bool HasFitContent() const {
    return false;
  }

  virtual bool IsNumber() const {
    return false;
  }
  virtual bool IsIdentifier() const {
    return false;
  }
  virtual bool IsSizingKeyword() const {
    return false;
  }
  virtual bool IsColorChannelKeyword() const {
    return false;
  }
  virtual bool IsPixelsAndPercent() const {
    return false;
  }
  virtual bool IsOperation() const {
    return false;
  }

  virtual std::shared_ptr<const CalculationExpressionNode> Zoom(double factor) const = 0;

  virtual ~CalculationExpressionNode() = default;

protected:
  virtual bool Equals(const CalculationExpressionNode& other) const = 0;

  bool has_content_or_intrinsic_ = false;
  bool has_auto_ = false;
  bool has_percent_ = false;
  bool has_stretch_ = false;
  bool has_color_channel_keyword_ = false;
};

class CalculationExpressionNumberNode final
    : public CalculationExpressionNode {
public:
  explicit CalculationExpressionNumberNode(float value)
      : value_(value) {
  }

  float Value() const {
    return value_;
  }

  // Implement |CalculationExpressionNode|:
  float Evaluate(float max_value, const EvaluationInput&) const final;
  bool Equals(const CalculationExpressionNode& other) const final;
  std::shared_ptr<const CalculationExpressionNode> Zoom(double factor) const final;
  bool IsNumber() const final {
    return true;
  }
  ~CalculationExpressionNumberNode() final = default;

private:
  float value_;
};

template <>
struct DowncastTraits<CalculationExpressionNumberNode> {
  static bool AllowFrom(const CalculationExpressionNode& node) {
    return node.IsNumber();
  }
};

class CalculationExpressionIdentifierNode final
    : public CalculationExpressionNode {
public:
  explicit CalculationExpressionIdentifierNode(AtomicString identifier)
      : identifier_(std::move(identifier)) {
  }

  const AtomicString& Value() const {
    return identifier_;
  }

  // Implement |CalculationExpressionNode|:
  float Evaluate(float max_value, const EvaluationInput&) const final {
    return 0.0f;
  }
  bool Equals(const CalculationExpressionNode& other) const final {
    auto* other_identifier =
        DynamicTo<CalculationExpressionIdentifierNode>(other);
    return other_identifier && other_identifier->Value() == Value();
  }
  std::shared_ptr<const CalculationExpressionNode> Zoom(double factor) const final {
    return shared_from_this();
  }
  bool IsIdentifier() const final {
    return true;
  }

private:
  AtomicString identifier_;
};

template <>
struct DowncastTraits<CalculationExpressionIdentifierNode> {
  static bool AllowFrom(const CalculationExpressionNode& node) {
    return node.IsIdentifier();
  }
};

class CalculationExpressionSizingKeywordNode final
    : public CalculationExpressionNode {
public:
  enum class Keyword : uint8_t {
    kSize,
    kAny,
    kAuto,
    kContent,

    // The keywords below should match those accepted by
    // css_parsing_utils::ValidWidthOrHeightKeyword.
    kMinContent,
    kWebkitMinContent,
    kMaxContent,
    kWebkitMaxContent,
    kFitContent,
    kWebkitFitContent,
    kStretch,
    kWebkitFillAvailable,
  };

  explicit CalculationExpressionSizingKeywordNode(Keyword keyword);

  Keyword Value() const {
    return keyword_;
  }

  // Implement |CalculationExpressionNode|:
  float Evaluate(float max_value, const EvaluationInput&) const final;
  bool Equals(const CalculationExpressionNode& other) const final {
    auto* other_sizing_keyword =
        DynamicTo<CalculationExpressionSizingKeywordNode>(other);
    return other_sizing_keyword && other_sizing_keyword->Value() == Value();
  }
  std::shared_ptr<const CalculationExpressionNode> Zoom(double factor) const final {
    // TODO(https://crbug.com/313072): Is this correct, or do we need to
    // adjust for zoom?
    return shared_from_this();
  }
  bool IsSizingKeyword() const final {
    return true;
  }

  bool HasMinContent() const final {
    return keyword_ == Keyword::kMinContent || keyword_ == Keyword::kWebkitMinContent;
  }
  bool HasMaxContent() const final {
    return keyword_ == Keyword::kMaxContent || keyword_ == Keyword::kWebkitMaxContent;
  }
  bool HasFitContent() const final {
    return keyword_ == Keyword::kFitContent || keyword_ == Keyword::kWebkitFitContent;
  }

private:
  Keyword keyword_;
};

template <>
struct DowncastTraits<CalculationExpressionSizingKeywordNode> {
  static bool AllowFrom(const CalculationExpressionNode& node) {
    return node.IsSizingKeyword();
  }
};

class CalculationExpressionColorChannelKeywordNode final
    : public CalculationExpressionNode {
public:
  explicit CalculationExpressionColorChannelKeywordNode(
      ColorChannelKeyword channel);

  ColorChannelKeyword Value() const {
    return channel_;
  }

  // Implement |CalculationExpressionNode|:
  float Evaluate(float max_value, const EvaluationInput&) const final;
  bool Equals(const CalculationExpressionNode& other) const final {
    auto* other_color_channel_keyword =
        DynamicTo<CalculationExpressionColorChannelKeywordNode>(other);
    return other_color_channel_keyword && other_color_channel_keyword->Value() == Value();
  }
  std::shared_ptr<const CalculationExpressionNode> Zoom(double factor) const final {
    return shared_from_this();
  }
  bool IsColorChannelKeyword() const final {
    return true;
  }

private:
  ColorChannelKeyword channel_;
};

template <>
struct DowncastTraits<CalculationExpressionColorChannelKeywordNode> {
  static bool AllowFrom(const CalculationExpressionNode& node) {
    return node.IsColorChannelKeyword();
  }
};

class CalculationExpressionPixelsAndPercentNode final
    : public CalculationExpressionNode {
public:
  explicit CalculationExpressionPixelsAndPercentNode(PixelsAndPercent value)
      : value_(value) {
    if (value.has_explicit_percent) {
      has_percent_ = true;
    }
  }

  float Pixels() const {
    return value_.pixels;
  }
  float Percent() const {
    return value_.percent;
  }
  PixelsAndPercent GetPixelsAndPercent() const {
    return value_;
  }
  bool HasExplicitPixels() const {
    return value_.has_explicit_pixels;
  }
  bool HasExplicitPercent() const {
    return value_.has_explicit_percent;
  }

  // Implement |CalculationExpressionNode|:
  float Evaluate(float max_value, const EvaluationInput&) const final;
  bool Equals(const CalculationExpressionNode& other) const final;
  std::shared_ptr<const CalculationExpressionNode> Zoom(double factor) const final;
  bool IsPixelsAndPercent() const final {
    return true;
  }
  ~CalculationExpressionPixelsAndPercentNode() final = default;

private:
  PixelsAndPercent value_;
};

template <>
struct DowncastTraits<CalculationExpressionPixelsAndPercentNode> {
  static bool AllowFrom(const CalculationExpressionNode& node) {
    return node.IsPixelsAndPercent();
  }
};

class CalculationExpressionOperationNode final
    : public CalculationExpressionNode {
public:
  using Children = Vector<std::shared_ptr<const CalculationExpressionNode>>;

  static std::shared_ptr<const CalculationExpressionNode> CreateSimplified(
      Children&& children,
      CalculationOperator op);

  CalculationExpressionOperationNode(Children&& children,
                                     CalculationOperator op);

  const Children& GetChildren() const {
    return children_;
  }
  CalculationOperator GetOperator() const {
    return operator_;
  }

  // Implement |CalculationExpressionNode|:
  float Evaluate(float max_value, const EvaluationInput&) const final;
  bool Equals(const CalculationExpressionNode& other) const final;
  std::shared_ptr<const CalculationExpressionNode> Zoom(double factor) const final;
  bool IsOperation() const final {
    return true;
  }
  bool HasMinContent() const final;
  bool HasMaxContent() const final;
  bool HasFitContent() const final;
  ~CalculationExpressionOperationNode() final = default;

private:
  Children children_;
  CalculationOperator operator_;
};

template <>
struct DowncastTraits<CalculationExpressionOperationNode> {
  static bool AllowFrom(const CalculationExpressionNode& node) {
    return node.IsOperation();
  }
};

} // namespace bkit
