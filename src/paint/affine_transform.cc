// Ported from: blink/renderer/platform/transforms/affine_transform.cc
/*
 * Copyright (C) 2005, 2006 Apple Computer, Inc.  All rights reserved.
 *               2010 Dirk Schulze <krit@webkit.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "affine_transform.h"

#include <cmath>

namespace bkit {

AffineTransform& AffineTransform::Scale(double s) {
  return Scale(s, s);
}

AffineTransform& AffineTransform::Scale(double sx, double sy) {
  transform_[0] *= sx;
  transform_[1] *= sx;
  transform_[2] *= sy;
  transform_[3] *= sy;
  return *this;
}

// *this = *this * translation
AffineTransform& AffineTransform::Translate(double tx, double ty) {
  transform_[4] += tx * transform_[0] + ty * transform_[2];
  transform_[5] += tx * transform_[1] + ty * transform_[3];
  return *this;
}

AffineTransform& AffineTransform::Shear(double sx, double sy) {
  double a = transform_[0];
  double b = transform_[1];

  transform_[0] += sy * transform_[2];
  transform_[1] += sy * transform_[3];
  transform_[2] += sx * a;
  transform_[3] += sx * b;

  return *this;
}

AffineTransform& AffineTransform::SkewY(double angle) {
  return Shear(0, std::tan(Deg2rad(angle)));
}

ScalarMatrix AffineTransform::ToScalarMatrix() const {
  // SkMatrices are 3x3, so they have a concept of "perspective" in the bottom
  // row. AffineTransform is a 2x3 matrix that can encode 2d rotations, skew
  // and translation, but has no perspective. Those parameters are set to zero
  // here. i.e.:
  //
  //   INPUT           OUTPUT
  // | a c e |       | a c e |
  // | b d f | ----> | b d f |
  //                 | 0 0 1 |
  return ScalarMatrix::MakeAll(ClampToFloat(A()), ClampToFloat(C()), ClampToFloat(E()),
                               ClampToFloat(B()), ClampToFloat(D()), ClampToFloat(F()));
}

} // namespace bkit
