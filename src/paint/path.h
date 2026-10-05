// Ported from: skia/include/core/SkPathBuilder.h
// Ported from: skia/src/core/SkPathBuilder.cpp

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "matrix.h"

namespace bkfont {

// The polynomial outline subset of SkPathBuilder used for glyph bounds.
// Bounds include control points, as SkPath::getBounds does, rather than curve
// extrema. Transform the points before measuring to avoid inflating bounds.
class ScalarPath {
public:
  enum class Verb : std::uint8_t {
    kMove,
    kLine,
    kQuad,
    kCubic,
    kClose
  };

  // SkPathFillType. Glyph outlines use the default winding rule.
  enum class FillType : std::uint8_t {
    kWinding,
    kEvenOdd,
  };

  FillType GetFillType() const {
    return fill_type_;
  }
  void SetFillType(FillType fill_type) {
    fill_type_ = fill_type;
  }

  bool IsEmpty() const {
    return verbs_.empty();
  }

  // SkPath::Rect, clockwise from the top left.
  static ScalarPath Rect(const ScalarRect& rect);
  // SkPath::Polygon.
  static ScalarPath Polygon(std::span<const ScalarPoint> points, bool is_closed);

  void MoveTo(ScalarPoint point);
  void LineTo(ScalarPoint point);
  void QuadTo(ScalarPoint control, ScalarPoint end);
  void CubicTo(ScalarPoint control1, ScalarPoint control2, ScalarPoint end);
  void Close();
  void Transform(const ScalarMatrix& matrix);
  ScalarRect GetBounds() const;

  // SkPath::makeOffset.
  ScalarPath MakeOffset(float dx, float dy) const;

  // SkPath::approximateBytesUsed: the object plus its point and verb storage.
  std::size_t ApproximateBytesUsed() const;

  std::span<const ScalarPoint> Points() const {
    return points_;
  }
  std::span<const Verb> Verbs() const {
    return verbs_;
  }

private:
  void EnsureMove();

  std::vector<ScalarPoint> points_;
  std::vector<Verb> verbs_;
  ScalarPoint last_move_;
  bool needs_move_ = true;
  FillType fill_type_ = FillType::kWinding;
};

} // namespace bkfont
