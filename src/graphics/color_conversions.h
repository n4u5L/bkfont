// Ported from: chromium/ui/gfx/color_conversions.h
// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <optional>
#include <tuple>

#include "color_float.h"

namespace bkfont::gfx {

using bkfont::ColorFloat4;

// All the methods below are exposed for bkfont::color conversions.

std::tuple<float, float, float> LabToXYZD50(float l, float a, float b);

std::tuple<float, float, float> XYZD50ToLab(float x, float y, float z);

std::tuple<float, float, float> OklabToXYZD65(float l, float a, float b);

std::tuple<float, float, float> XYZD65ToOklab(float x, float y, float z);

std::tuple<float, float, float> LchToLab(float l, float c, float h);

std::tuple<float, float, float> LabToLch(float l, float a, float b);

std::tuple<float, float, float> DisplayP3ToXYZD50(float r, float g, float b);

std::tuple<float, float, float> XYZD50ToDisplayP3(float x, float y, float z);

std::tuple<float, float, float> ProPhotoToXYZD50(float r, float g, float b);

std::tuple<float, float, float> XYZD50ToProPhoto(float x, float y, float z);

std::tuple<float, float, float> AdobeRGBToXYZD50(float r, float g, float b);

std::tuple<float, float, float> XYZD50ToAdobeRGB(float x, float y, float z);

std::tuple<float, float, float> Rec2020ToXYZD50(float r, float g, float b);

std::tuple<float, float, float> XYZD50ToRec2020(float x, float y, float z);

std::tuple<float, float, float> XYZD50ToD65(float x, float y, float z);

std::tuple<float, float, float> XYZD65ToD50(float x, float y, float z);

std::tuple<float, float, float> XYZD65TosRGBLinear(float x, float y, float z);

std::tuple<float, float, float> SRGBToSRGBLegacy(float r, float g, float b);

std::tuple<float, float, float> SRGBLegacyToSRGB(float r, float g, float b);

std::tuple<float, float, float> XYZD50TosRGB(float x, float y, float z);

std::tuple<float, float, float> XYZD50TosRGBLinear(float x, float y, float z);

std::tuple<float, float, float> SRGBLinearToXYZD50(float r, float g, float b);

std::tuple<float, float, float> SRGBToXYZD50(float r, float g, float b);

std::tuple<float, float, float> HSLToSRGB(float h, float s, float l);

std::tuple<float, float, float> SRGBToHSL(float r, float g, float b);

std::tuple<float, float, float> HWBToSRGB(float h, float w, float b);

std::tuple<float, float, float> SRGBToHWB(float r, float g, float b);

ColorFloat4 XYZD50ToColorFloat4(float x, float y, float z, float alpha);

ColorFloat4 XYZD65ToColorFloat4(float x, float y, float z, float alpha);

ColorFloat4 LabToColorFloat4(float l, float a, float b, float alpha);

ColorFloat4 OklabToColorFloat4(float l, float a, float b, float alpha);

ColorFloat4 OklabGamutMapToColorFloat4(float l, float a, float b, float alpha);

ColorFloat4 LchToColorFloat4(float l, float a, float b, float alpha);

ColorFloat4 OklchToColorFloat4(float l, float a, float h, float alpha);

ColorFloat4 OklchGamutMapToColorFloat4(float l, float a, float h, float alpha);

ColorFloat4 SRGBLinearToColorFloat4(float r, float g, float b, float alpha);

ColorFloat4 ProPhotoToColorFloat4(float r, float g, float b, float alpha);

ColorFloat4 DisplayP3ToColorFloat4(float r, float g, float b, float alpha);

ColorFloat4 AdobeRGBToColorFloat4(float r, float g, float b, float alpha);

ColorFloat4 Rec2020ToColorFloat4(float r, float g, float b, float alpha);

ColorFloat4 HSLToColorFloat4(float h, float s, float l, float alpha);

ColorFloat4 HWBToColorFloat4(float h, float w, float b, float alpha);

} // namespace bkfont::gfx
