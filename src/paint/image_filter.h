// Ported from: skia/src/effects/imagefilters/SkDropShadowImageFilter.cpp
// Ported from: skia/src/effects/imagefilters/SkMergeImageFilter.cpp
// Local float-image backend for the image-filter graph used by text shadows.
#pragma once

#include <memory>
#include <span>
#include <vector>

#include "color4f.h"
#include "matrix.h"

namespace bkit {

struct FilterImage {
  IntRect bounds;
  std::vector<PMColor4f> pixels;
  PMColor4f At(int x, int y) const;
  PMColor4f Sample(float x, float y) const;
};

class ImageFilter {
public:
  virtual ~ImageFilter() = default;
  // Bounds are in device space; the matrix maps filter parameters from local
  // space. Pixels outside the source are transparent (decal tiling).
  virtual IntRect RequiredInput(const IntRect& output, const ScalarMatrix&) const = 0;
  virtual FilterImage Apply(const FilterImage&, const ScalarMatrix&, const IntRect& output) const = 0;

  static std::shared_ptr<const ImageFilter> DropShadow(float dx, float dy, float sigma_x, float sigma_y,
                                                       Color4f, bool shadow_only = true);
  // Children are composited in order using src-over. A null child is source.
  static std::shared_ptr<const ImageFilter> Merge(std::span<const std::shared_ptr<const ImageFilter>>);
};

} // namespace bkit
