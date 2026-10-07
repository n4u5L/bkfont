// Ported from: skia/modules/skcms/skcms.cc
// Ported from: skia/modules/skcms/src/skcms_public.h
// Ported from: skia/include/core/SkColorSpace.h
// Ported from: skia/include/private/base/SkFixed.h
// Scalar transfer and matrix routines.
// No Skia or skcms binary/header dependency.

/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#pragma once
namespace bkit::color_math {

struct skcms_Matrix3x3 {
  float vals[3][3];
};
struct skcms_TransferFunction {
  float g, a, b, c, d, e, f;
};
enum skcms_TFType {
  skcms_TFType_Invalid,
  skcms_TFType_sRGBish,
  skcms_TFType_PQish,
  skcms_TFType_HLGish,
  skcms_TFType_HLGinvish,
  skcms_TFType_PQ,
  skcms_TFType_HLG,
};
float skcms_TransferFunction_eval(const skcms_TransferFunction*, float);
bool skcms_TransferFunction_invert(const skcms_TransferFunction*, skcms_TransferFunction*);
bool skcms_AdaptToXYZD50(float, float, skcms_Matrix3x3*);
bool skcms_PrimariesToXYZD50(float, float, float, float, float, float, float, float,
                             skcms_Matrix3x3*);
bool skcms_Matrix3x3_invert(const skcms_Matrix3x3*, skcms_Matrix3x3*);
skcms_Matrix3x3 skcms_Matrix3x3_concat(const skcms_Matrix3x3*, const skcms_Matrix3x3*);

// Constants extracted unchanged.
// Copyright 2016 Google Inc.; BSD-style license, see LICENSE.
namespace NamedTransferFn {

inline constexpr skcms_TransferFunction kSRGB =
    {2.4f, (float)(1 / 1.055), (float)(0.055 / 1.055), (float)(1 / 12.92), 0.04045f, 0.f, 0.f};
inline constexpr skcms_TransferFunction k2Dot2 = {2.2f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f};
inline constexpr skcms_TransferFunction kRec2020 =
    {2.22222f, 0.909672f, 0.0903276f, 0.222222f, 0.0812429f, 0, 0};
inline constexpr skcms_TransferFunction kProPhotoRGB = {1.8f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f};

} // namespace NamedTransferFn

namespace NamedGamut {

// Exact SkFixedToFloat scaling.
constexpr float FixedToFloat(int value) {
  return value * 1.52587890625e-5f;
}
inline constexpr skcms_Matrix3x3 kSRGB = {{{FixedToFloat(0x6FA2), FixedToFloat(0x6299), FixedToFloat(0x24A0)},
                                           {FixedToFloat(0x38F5), FixedToFloat(0xB785), FixedToFloat(0x0F84)},
                                           {FixedToFloat(0x0390), FixedToFloat(0x18DA), FixedToFloat(0xB6CF)}}};
inline constexpr skcms_Matrix3x3 kAdobeRGB = {{{FixedToFloat(0x9c18), FixedToFloat(0x348d), FixedToFloat(0x2631)},
                                               {FixedToFloat(0x4fa5), FixedToFloat(0xa02c), FixedToFloat(0x102f)},
                                               {FixedToFloat(0x04fc), FixedToFloat(0x0f95), FixedToFloat(0xbe9c)}}};
inline constexpr skcms_Matrix3x3 kDisplayP3 = {{{0.515102f, 0.291965f, 0.157153f},
                                                {0.241182f, 0.692236f, 0.0665819f},
                                                {-0.00104941f, 0.0418818f, 0.784378f}}};
inline constexpr skcms_Matrix3x3 kRec2020 = {{{0.673459f, 0.165661f, 0.125100f},
                                              {0.279033f, 0.675338f, 0.0456288f},
                                              {-0.00193139f, 0.0299794f, 0.797162f}}};

} // namespace NamedGamut

} // namespace bkit::color_math
