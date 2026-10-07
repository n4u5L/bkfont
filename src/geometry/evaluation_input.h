// Ported from: blink/renderer/platform/geometry/evaluation_input.h
// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>
#include <optional>

#include "base/containers/flat_map.h"
#include "base/functional/function_ref.h"
#include "color_channel_keyword.h"
#include "layout/layout_unit.h"

namespace bkit {

class Length;

// When calcuating the min/max content-contribution we sometimes need to coerce
// a fit-content/stretch basis to auto.
enum class CalcSizeKeywordBehavior {
  kAsSpecified,
  kAsAuto
};

struct EvaluationInput {

  using IntrinsicLengthEvaluator = base::FunctionRef<bkit::LayoutUnit(const bkit::Length&)>;

public:
  std::optional<float> size_keyword_basis = std::nullopt;
  std::function<LayoutUnit(const Length&)> intrinsic_evaluator;
  CalcSizeKeywordBehavior calc_size_keyword_behavior =
      CalcSizeKeywordBehavior::kAsSpecified;
  base::flat_map<ColorChannelKeyword, float> color_channel_keyword_values;
};

} // namespace bkit
