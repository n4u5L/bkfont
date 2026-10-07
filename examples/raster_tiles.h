#pragma once

#include <algorithm>
#include <optional>

#include "paint/raster_canvas.h"

namespace bkfont::example {

// Keep float drawing storage bounded independently of window size and DPI.
// Every tile uses global device coordinates: clipping must not translate
// paths or change glyph snapping. Filter layers obtain their input halo via
// RasterCanvas::SaveLayer, including pixels outside the tile.
template <class Paint>
void PaintTiles(const Pixmap& pixels, IntRect area, RasterCanvas::ScratchBuffer& scratch,
                std::optional<ColorARGB> clear, const Paint& paint) {
  constexpr int kMaxPixels = 128 * 1024; // 2 MiB of PMColor4f pixels.
  if (area.IsEmpty()) return;
  const int columns = std::min(area.Width(), kMaxPixels);
  const int rows = std::max(1, kMaxPixels / columns);
  for (int top = area.top; top < area.bottom; top += rows) {
    for (int left = area.left; left < area.right; left += columns) {
      const IntRect tile = IntRect::MakeLTRB(left, top, std::min(left + columns, area.right), std::min(top + rows, area.bottom));
      const Pixmap region(pixels.GetColorType(), tile.Width(), tile.Height(), pixels.WritableAddr8(left, top), pixels.RowBytes());
      RasterCanvas canvas(region, SurfaceProps(), clear, left, top, &scratch);
      paint(canvas, tile);
    }
  }
}

} // namespace bkfont::example
