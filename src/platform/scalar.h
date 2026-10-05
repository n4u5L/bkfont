// Ported from: skia/include/core/SkScalar.h
// Ported from: skia/include/private/base/SkFloatingPoint.h
// Ported from: skia/include/private/base/SkFixed.h
// Ported from: skia/src/core/SkFDot6.h

#pragma once

#include <cmath>
#include <cstdint>

namespace bkfont {

// SK_ScalarNearlyZero.
inline constexpr float kScalarNearlyZero = 1.0f / (1 << 12);

// SK_ScalarSinCosNearlyZero, used by SkMatrix's rotation snapping.
inline constexpr float kScalarSinCosNearlyZero = 1.0f / (1 << 16);

inline constexpr int kMaxS32FitsInFloat = 2147483520;
inline constexpr int kMinS32FitsInFloat = -kMaxS32FitsInFloat;

// sk_float_saturate2int. Returns kMaxS32FitsInFloat for NaN.
inline int FloatSaturateToInt(float x) {
#if defined(_MSC_VER) && !defined(__clang__)
  // MSVC 19.38+ mishandles NaN in the comparisons below.
  if (std::isnan(x)) return kMaxS32FitsInFloat;
#endif
  x = x < static_cast<float>(kMaxS32FitsInFloat) ? x : static_cast<float>(kMaxS32FitsInFloat);
  x = x > static_cast<float>(kMinS32FitsInFloat) ? x : static_cast<float>(kMinS32FitsInFloat);
  return static_cast<int>(x);
}

// sk_float_round2int, which SkScalarRoundToInt expands to. The rounding is
// done in double.
inline int FloatRoundToInt(float x) {
  return FloatSaturateToInt(static_cast<float>(std::floor(static_cast<double>(x) + 0.5)));
}

// SkFloatToFixed, which SkScalarToFixed expands to.
inline std::int32_t FloatToFixed(float x) {
  return FloatSaturateToInt(x * (1 << 16));
}

// SkFixedToFloat, which SkFixedToScalar expands to. A macro upstream, so a
// 64-bit FT_Fixed is converted to float without narrowing first.
template <typename T>
constexpr float FixedToFloat(T x) {
  return static_cast<float>(x) * 1.52587890625e-5f;
}

// SkFloatToFDot6, which SkScalarToFDot6 expands to.
inline std::int32_t FloatToFDot6(float x) {
  return static_cast<std::int32_t>(x * 64);
}

// SkFDot6ToFloat, which SkFDot6ToScalar expands to.
template <typename T>
constexpr float FDot6ToFloat(T x) {
  return static_cast<float>(x) * 0.015625f;
}

} // namespace bkfont
