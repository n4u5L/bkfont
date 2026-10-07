// Ported from: chromium/base/numerics/angle_conversions.h
// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <concepts>
#include <numbers>

namespace bkit::base {

template <typename T>
  requires std::floating_point<T>
constexpr T DegToRad(T deg) {
  return deg * std::numbers::pi_v<T> / 180;
}

template <typename T>
  requires std::floating_point<T>
constexpr T RadToDeg(T rad) {
  return rad * 180 / std::numbers::pi_v<T>;
}

} // namespace bkit::base
