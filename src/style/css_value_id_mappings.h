// Ported from: blink/renderer/core/css/css_value_id_mappings.h
// Ported from: blink/renderer/core/css/css_identifier_value_mappings.h
// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
//
// CSSIdentifierValue::ConvertTo<T>() and its reverse for the ported enums.
// Keyword fields use the generated name-based mappings; the specializations
// below are the upstream special cases and the font enums, which are not
// ComputedStyle fields here.
#pragma once

#include "base/notreached.h"
#include "font/font_description.h"
#include "font/font_size_adjust.h"
#include "shaping/text_spacing_trim.h"
#include "style/computed_style_constants.h"
#include "style/css_identifier_value.h"
#include "style/css_value_id_mappings_generated.h"

namespace bkit {

template <class T>
inline T CssValueIDToPlatformEnum(CSSValueID v) {
  // By default, we use the generated mappings. For special cases, we
  // specialize.
  return detail::cssValueIDToPlatformEnumGenerated<T>(v);
}

template <class T>
inline CSSValueID PlatformEnumToCSSValueID(T v) {
  return detail::platformEnumToCSSValueIDGenerated(v);
}

template <>
inline UnicodeBidi CssValueIDToPlatformEnum(CSSValueID v) {
  if (v == CSSValueID::kWebkitIsolate) return UnicodeBidi::kIsolate;
  if (v == CSSValueID::kWebkitIsolateOverride) return UnicodeBidi::kIsolateOverride;
  if (v == CSSValueID::kWebkitPlaintext) return UnicodeBidi::kPlaintext;
  return detail::cssValueIDToPlatformEnumGenerated<UnicodeBidi>(v);
}

template <>
inline ETextCombine CssValueIDToPlatformEnum(CSSValueID v) {
  if (v == CSSValueID::kHorizontal) return ETextCombine::kAll; // -webkit-text-combine
  return detail::cssValueIDToPlatformEnumGenerated<ETextCombine>(v);
}

template <>
inline ETextAlign CssValueIDToPlatformEnum(CSSValueID v) {
  // Legacy -webkit-auto. Eqiuvalent to start.
  if (v == CSSValueID::kWebkitAuto) return ETextAlign::kStart;
  if (v == CSSValueID::kInternalCenter) return ETextAlign::kCenter;
  return detail::cssValueIDToPlatformEnumGenerated<ETextAlign>(v);
}

template <>
inline ETextOrientation CssValueIDToPlatformEnum(CSSValueID v) {
  if (v == CSSValueID::kSidewaysRight) return ETextOrientation::kSideways;
  if (v == CSSValueID::kVerticalRight) return ETextOrientation::kMixed;
  return detail::cssValueIDToPlatformEnumGenerated<ETextOrientation>(v);
}

template <>
inline WritingMode CssValueIDToPlatformEnum(CSSValueID v) {
  switch (v) {
    case CSSValueID::kLr:
    case CSSValueID::kLrTb:
    case CSSValueID::kRl:
    case CSSValueID::kRlTb: return WritingMode::kHorizontalTb;
    case CSSValueID::kTb:
    case CSSValueID::kTbRl: return WritingMode::kVerticalRl;
    default: break;
  }
  return detail::cssValueIDToPlatformEnumGenerated<WritingMode>(v);
}

template <>
inline EVerticalAlign CssValueIDToPlatformEnum(CSSValueID v) {
  switch (v) {
    case CSSValueID::kTop: return EVerticalAlign::kTop;
    case CSSValueID::kBottom: return EVerticalAlign::kBottom;
    case CSSValueID::kMiddle: return EVerticalAlign::kMiddle;
    case CSSValueID::kBaseline: return EVerticalAlign::kBaseline;
    case CSSValueID::kTextBottom: return EVerticalAlign::kTextBottom;
    case CSSValueID::kTextTop: return EVerticalAlign::kTextTop;
    case CSSValueID::kSub: return EVerticalAlign::kSub;
    case CSSValueID::kSuper: return EVerticalAlign::kSuper;
    case CSSValueID::kWebkitBaselineMiddle: return EVerticalAlign::kBaselineMiddle;
    default: break;
  }
  NOTREACHED();
}

template <>
inline TextEmphasisMark CssValueIDToPlatformEnum(CSSValueID v) {
  switch (v) {
    case CSSValueID::kNone: return TextEmphasisMark::kNone;
    case CSSValueID::kDot: return TextEmphasisMark::kDot;
    case CSSValueID::kCircle: return TextEmphasisMark::kCircle;
    case CSSValueID::kDoubleCircle: return TextEmphasisMark::kDoubleCircle;
    case CSSValueID::kTriangle: return TextEmphasisMark::kTriangle;
    case CSSValueID::kSesame: return TextEmphasisMark::kSesame;
    default: break;
  }
  NOTREACHED();
}

template <>
inline FontSizeAdjust::Metric CssValueIDToPlatformEnum(CSSValueID v) {
  switch (v) {
    case CSSValueID::kExHeight: return FontSizeAdjust::Metric::kExHeight;
    case CSSValueID::kCapHeight: return FontSizeAdjust::Metric::kCapHeight;
    case CSSValueID::kChWidth: return FontSizeAdjust::Metric::kChWidth;
    case CSSValueID::kIcWidth: return FontSizeAdjust::Metric::kIcWidth;
    case CSSValueID::kIcHeight: return FontSizeAdjust::Metric::kIcHeight;
    default: break;
  }
  NOTREACHED();
}

template <>
inline FontDescription::FontSynthesisWeight CssValueIDToPlatformEnum(CSSValueID v) {
  switch (v) {
    case CSSValueID::kAuto: return FontDescription::kAutoFontSynthesisWeight;
    case CSSValueID::kNone: return FontDescription::kNoneFontSynthesisWeight;
    default: break;
  }
  NOTREACHED();
}

template <>
inline FontDescription::FontSynthesisStyle CssValueIDToPlatformEnum(CSSValueID v) {
  switch (v) {
    case CSSValueID::kAuto: return FontDescription::kAutoFontSynthesisStyle;
    case CSSValueID::kNone: return FontDescription::kNoneFontSynthesisStyle;
    default: break;
  }
  NOTREACHED();
}

template <>
inline FontDescription::FontSynthesisSmallCaps CssValueIDToPlatformEnum(CSSValueID v) {
  switch (v) {
    case CSSValueID::kAuto: return FontDescription::kAutoFontSynthesisSmallCaps;
    case CSSValueID::kNone: return FontDescription::kNoneFontSynthesisSmallCaps;
    default: break;
  }
  NOTREACHED();
}

template <>
inline FontSmoothingMode CssValueIDToPlatformEnum(CSSValueID v) {
  switch (v) {
    case CSSValueID::kAuto: return kAutoSmoothing;
    case CSSValueID::kNone: return kNoSmoothing;
    case CSSValueID::kAntialiased: return kAntialiased;
    case CSSValueID::kSubpixelAntialiased: return kSubpixelAntialiased;
    default: break;
  }
  NOTREACHED();
}

template <>
inline FontVariantEmoji CssValueIDToPlatformEnum(CSSValueID v) {
  switch (v) {
    case CSSValueID::kNormal: return kNormalVariantEmoji;
    case CSSValueID::kText: return kTextVariantEmoji;
    case CSSValueID::kEmoji: return kEmojiVariantEmoji;
    case CSSValueID::kUnicode: return kUnicodeVariantEmoji;
    default: break;
  }
  NOTREACHED();
}

template <>
inline TextRenderingMode CssValueIDToPlatformEnum(CSSValueID v) {
  switch (v) {
    case CSSValueID::kAuto: return kAutoTextRendering;
    case CSSValueID::kOptimizespeed: return kOptimizeSpeed;
    case CSSValueID::kOptimizelegibility: return kOptimizeLegibility;
    case CSSValueID::kGeometricprecision: return kGeometricPrecision;
    default: break;
  }
  NOTREACHED();
}

// TextSpacingTrim is not a field here; this is its generated mapping.
template <>
inline TextSpacingTrim CssValueIDToPlatformEnum(CSSValueID v) {
  switch (v) {
    case CSSValueID::kNormal: return TextSpacingTrim::kNormal;
    case CSSValueID::kSpaceAll: return TextSpacingTrim::kSpaceAll;
    case CSSValueID::kSpaceFirst: return TextSpacingTrim::kSpaceFirst;
    case CSSValueID::kTrimStart: return TextSpacingTrim::kTrimStart;
    default: break;
  }
  NOTREACHED();
}

} // namespace bkit
