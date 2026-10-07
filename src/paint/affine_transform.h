// Ported from: blink/renderer/platform/transforms/affine_transform.h
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

#pragma once

#include "base/math_extras.h"
#include "matrix.h"

namespace bkit {

// Only the members used by the vertical text paint helpers are ported.
// ToScalarMatrix() replaces ToSkMatrix(); both drop the perspective row.
class AffineTransform {
public:
  constexpr AffineTransform()
      : transform_{1, 0, 0, 1, 0, 0} {
  }
  constexpr AffineTransform(double a, double b, double c, double d, double e, double f)
      : transform_{a, b, c, d, e, f} {
  }

  void SetMatrix(double a, double b, double c, double d, double e, double f) {
    *this = AffineTransform(a, b, c, d, e, f);
  }

  bool IsIdentity() const {
    return transform_[0] == 1 && transform_[1] == 0 && transform_[2] == 0 && transform_[3] == 1 &&
           transform_[4] == 0 && transform_[5] == 0;
  }

  double A() const {
    return transform_[0];
  }
  double B() const {
    return transform_[1];
  }
  double C() const {
    return transform_[2];
  }
  double D() const {
    return transform_[3];
  }
  double E() const {
    return transform_[4];
  }
  double F() const {
    return transform_[5];
  }

  void MakeIdentity() {
    *this = AffineTransform();
  }

  AffineTransform& Scale(double);
  AffineTransform& Scale(double sx, double sy);
  AffineTransform& Translate(double tx, double ty);
  AffineTransform& Shear(double sx, double sy);
  AffineTransform& SkewY(double angle);

  bool operator==(const AffineTransform& m2) const {
    return transform_[0] == m2.transform_[0] && transform_[1] == m2.transform_[1] &&
           transform_[2] == m2.transform_[2] && transform_[3] == m2.transform_[3] &&
           transform_[4] == m2.transform_[4] && transform_[5] == m2.transform_[5];
  }

  ScalarMatrix ToScalarMatrix() const;

private:
  static float ClampToFloat(double value) {
    return ClampToWithNaNTo0<float>(value);
  }

  double transform_[6];
};

} // namespace bkit
