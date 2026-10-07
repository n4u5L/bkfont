// Ported from: blink/renderer/core/layout/geometry/physical_rect.h
// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "geometry/physical_offset.h"
#include "geometry/physical_size.h"
#include "layout/layout_unit.h"

namespace bkit {

// PhysicalRect is the position and size of a rect (typically a fragment)
// relative to its parent rect in the physical coordinate system.
// For more information about physical and logical coordinate systems, see:
// https://chromium.googlesource.com/chromium/src/+/main/third_party/blink/renderer/core/layout/README.md#coordinate-spaces
//
// Only the members used by the vertical text layout helpers are ported.
struct PhysicalRect {
  constexpr PhysicalRect() = default;
  constexpr PhysicalRect(const PhysicalOffset& offset, const PhysicalSize& size)
      : offset(offset),
        size(size) {
  }
  constexpr PhysicalRect(LayoutUnit left, LayoutUnit top, LayoutUnit width, LayoutUnit height)
      : offset(left, top),
        size(width, height) {
  }

  // This is deleted to avoid unwanted lossy conversion from float or double to
  // LayoutUnit or int. Use explicit LayoutUnit constructor for each parameter,
  // or use EnclosingRect() or FastAndLossyFromRectF() instead.
  PhysicalRect(double, double, double, double) = delete;

  PhysicalOffset offset;
  PhysicalSize size;

  constexpr bool IsEmpty() const {
    return size.IsEmpty();
  }

  constexpr LayoutUnit X() const {
    return offset.left;
  }
  constexpr LayoutUnit Y() const {
    return offset.top;
  }
  constexpr LayoutUnit Width() const {
    return size.width;
  }
  constexpr LayoutUnit Height() const {
    return size.height;
  }
  LayoutUnit Right() const {
    return offset.left + size.width;
  }
  LayoutUnit Bottom() const {
    return offset.top + size.height;
  }

  void SetX(LayoutUnit x) {
    offset.left = x;
  }
  void SetY(LayoutUnit y) {
    offset.top = y;
  }
  void SetWidth(LayoutUnit w) {
    size.width = w;
  }
  void SetHeight(LayoutUnit h) {
    size.height = h;
  }

  PhysicalOffset MinXMinYCorner() const {
    return offset;
  }
  PhysicalOffset MaxXMinYCorner() const {
    return {offset.left + size.width, offset.top};
  }
  PhysicalOffset MinXMaxYCorner() const {
    return {offset.left, offset.top + size.height};
  }
  PhysicalOffset MaxXMaxYCorner() const {
    return {offset.left + size.width, offset.top + size.height};
  }

  constexpr bool operator==(const PhysicalRect& other) const = default;

  // Whether all edges of the rect are at full-pixel boundaries.
  // i.e.: ToEnclosingRect(this)) == this
  bool EdgesOnPixelBoundaries() const {
    return !offset.left.HasFraction() && !offset.top.HasFraction() && !size.width.HasFraction() &&
           !size.height.HasFraction();
  }

  void Move(const PhysicalOffset& o) {
    offset += o;
  }

  // TODO(crbug.com/962299): These functions should upgraded to force correct
  // pixel snapping in a type-safe way.
  Point PixelSnappedOffset() const {
    return ToRoundedPoint(offset);
  }
  int PixelSnappedWidth() const {
    return SnapSizeToPixel(size.width, offset.left);
  }
  int PixelSnappedHeight() const {
    return SnapSizeToPixel(size.height, offset.top);
  }
  Size PixelSnappedSize() const {
    return {PixelSnappedWidth(), PixelSnappedHeight()};
  }

  constexpr explicit operator RectF() const {
    return RectF(offset.left, offset.top, size.width, size.height);
  }

  static PhysicalRect EnclosingRect(const RectF& rect) {
    PhysicalOffset offset(LayoutUnit::FromFloatFloor(rect.x()), LayoutUnit::FromFloatFloor(rect.y()));
    PhysicalSize size(LayoutUnit::FromFloatCeil(rect.right()) - offset.left,
                      LayoutUnit::FromFloatCeil(rect.bottom()) - offset.top);
    return PhysicalRect(offset, size);
  }

  // This is faster than EnclosingRect(). Can be used in situation that we
  // prefer performance to accuracy and haven't observed problems caused by the
  // tiny error (< LayoutUnit::Epsilon()).
  static PhysicalRect FastAndLossyFromRectF(const RectF& rect) {
    return PhysicalRect(LayoutUnit(rect.x()), LayoutUnit(rect.y()), LayoutUnit(rect.width()),
                        LayoutUnit(rect.height()));
  }

  explicit PhysicalRect(const Rect& r)
      : offset(r.origin()),
        size(r.size()) {
  }
};

// TODO(crbug.com/962299): These functions should upgraded to force correct
// pixel snapping in a type-safe way.
inline Rect ToEnclosingRect(const PhysicalRect& r) {
  Point location = ToFlooredPoint(r.offset);
  Point max_point = ToCeiledPoint(r.MaxXMaxYCorner());
  // Because the range of LayoutUnit is much smaller than int, the following
  // '-' operations can never overflow, so no clamping is needed.
  // TODO(1261553): We can have a special version of gfx::Rect constructor that
  // skips internal clamping to improve performance.
  return Rect(location.x(), location.y(), max_point.x() - location.x(), max_point.y() - location.y());
}
inline Rect ToPixelSnappedRect(const PhysicalRect& r) {
  return {r.PixelSnappedOffset(), r.PixelSnappedSize()};
}

} // namespace bkit
