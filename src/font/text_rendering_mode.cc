// Port source: third_party/blink/renderer/platform/fonts/text_rendering_mode.cc
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "text_rendering_mode.h"

#include "base/text/wtf_string.h"

namespace bkfont {

String ToString(TextRenderingMode mode) {
  switch (mode) {
  case TextRenderingMode::kAutoTextRendering:
    return "Auto";
  case TextRenderingMode::kOptimizeSpeed:
    return "OptimizeSpeed";
  case TextRenderingMode::kOptimizeLegibility:
    return "OptimizeLegibility";
  case TextRenderingMode::kGeometricPrecision:
    return "GeometricPrecision";
  }
  return "Unknown";
}

String ToStringForIdl(TextRenderingMode mode) {
  switch (mode) {
  case TextRenderingMode::kAutoTextRendering:
    return "auto";
  case TextRenderingMode::kOptimizeSpeed:
    return "optimizeSpeed";
  case TextRenderingMode::kOptimizeLegibility:
    return "optimizeLegibility";
  case TextRenderingMode::kGeometricPrecision:
    return "geometricPrecision";
  }
  return "Unknown";
}

} // namespace bkfont
