// Local implementation: scan conversion of filled paths for the CPU canvas,
// standing in for skia/src/core/SkScan_AntiPath.cpp and SkScan_Path.cpp.
// Anti-aliased scan conversion resolves winding/even-odd intervals before
// accumulating their area, splitting at vertices and edge intersections.
// Curves are flattened; aliased coverage samples pixel centers. Results are
// not bit-exact with Skia's scan converters.

#pragma once

#include <vector>

#include "matrix.h"
#include "path.h"
#include "rect.h"

namespace bkfont {

// Coverage in [0, 1] for each pixel of a device rect.
struct CoverageMask {
  IntRect bounds;
  std::vector<float> coverage;

  bool IsEmpty() const {
    return bounds.IsEmpty();
  }
  float At(int x, int y) const {
    return coverage[static_cast<std::size_t>(y - bounds.top) * static_cast<std::size_t>(bounds.Width()) + static_cast<std::size_t>(x - bounds.left)];
  }
};

// Fills the path mapped by matrix, limited to clip. Open contours are closed
// implicitly. The mask bounds are the path bounds intersected with clip; they
// are empty when nothing is covered.
void RasterizePath(const ScalarPath& path, const ScalarMatrix& matrix, const IntRect& clip,
                   bool anti_alias, CoverageMask* mask);

} // namespace bkfont
