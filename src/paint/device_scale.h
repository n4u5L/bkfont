// Host boundary corresponding to WidgetBase's DIP/Blink-space conversions.
// Layout and paint use framebuffer pixels. DSF enters style resolution as
// layout zoom, never as an additional root-canvas transform.
#pragma once

#include <cassert>
#include <cmath>

#include "geometry.h"

namespace bkfont {

struct DeviceScale {
  // Chromium's screen device_scale_factor is a scalar, independent of the
  // window/framebuffer size ratio and of page zoom.
  float factor = 1;

  bool IsValid() const {
    return std::isfinite(factor) && factor > 0;
  }

  float DIPsToLayout(float value) const {
    assert(IsValid());
    return value * factor;
  }

  float LayoutToDIPs(float value) const {
    assert(IsValid());
    return value * (1.f / factor);
  }

  PointF DIPsToLayout(const PointF& point) const {
    return PointF(DIPsToLayout(point.x()), DIPsToLayout(point.y()));
  }
  PointF LayoutToDIPs(const PointF& point) const {
    return PointF(LayoutToDIPs(point.x()), LayoutToDIPs(point.y()));
  }
  SizeF DIPsToLayout(const SizeF& size) const {
    return SizeF(DIPsToLayout(size.width()), DIPsToLayout(size.height()));
  }
  SizeF LayoutToDIPs(const SizeF& size) const {
    return SizeF(LayoutToDIPs(size.width()), LayoutToDIPs(size.height()));
  }
  RectF DIPsToLayout(const RectF& rect) const {
    return RectF(DIPsToLayout(rect.origin()), DIPsToLayout(rect.size()));
  }
  // Convert widget-relative caret/composition bounds to host DIPs only after
  // including the content paint offset (including scrolling).
  RectF LayoutToDIPs(const RectF& rect) const {
    return RectF(LayoutToDIPs(rect.origin()), LayoutToDIPs(rect.size()));
  }

  // Window coordinates are host-defined: physical on Win32/X11, typically
  // logical on Wayland. Convert through the actual framebuffer dimensions;
  // its ratio to the window size need not equal the display scale (including
  // fractional scaling and rounding). Both sizes must be non-empty.
  PointF WindowToLayout(double window_x, double window_y,
                        const Size& window_size, const Size& framebuffer_size) const {
    assert(IsValid() && !window_size.IsEmpty() && !framebuffer_size.IsEmpty());
    return PointF(static_cast<float>(window_x * framebuffer_size.width() / window_size.width()),
                  static_cast<float>(window_y * framebuffer_size.height() / window_size.height()));
  }

  PointF LayoutToWindow(const PointF& point, const Size& window_size, const Size& framebuffer_size) const {
    assert(IsValid() && !window_size.IsEmpty() && !framebuffer_size.IsEmpty());
    return PointF(static_cast<float>(static_cast<double>(point.x()) * window_size.width() / framebuffer_size.width()),
                  static_cast<float>(static_cast<double>(point.y()) * window_size.height() / framebuffer_size.height()));
  }

  RectF LayoutToWindow(const RectF& rect, const Size& window_size, const Size& framebuffer_size) const {
    const PointF origin = LayoutToWindow(rect.origin(), window_size, framebuffer_size);
    const PointF size = LayoutToWindow(PointF(rect.width(), rect.height()), window_size, framebuffer_size);
    return RectF(origin, SizeF(size.x(), size.y()));
  }
};

} // namespace bkfont
