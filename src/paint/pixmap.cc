// Ported from: skia/src/core/SkMipmap.cpp
// Ported from: skia/src/core/SkMipmapHQDownSampler.cpp

#include "pixmap.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

#include "mask.h"

namespace bkit {

namespace {

// The channels of one pixel widened for filtering, as the F::Expand of the
// color type filters.
struct Wide {
  std::array<std::uint32_t, 4> c{};

  Wide operator+(const Wide& other) const {
    Wide result;
    for (std::size_t i = 0; i < 4; ++i) {
      result.c[i] = c[i] + other.c[i];
    }
    return result;
  }
};

Wide Expand(const Pixmap& pm, const std::uint8_t* row, int x) {
  Wide result;
  if (pm.GetColorType() == ColorType::kAlpha8) {
    result.c[0] = row[x];
  } else {
    const std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
    for (std::size_t i = 0; i < 4; ++i) {
      result.c[i] = p[i];
    }
  }
  return result;
}

void Compact(const Pixmap& pm, std::uint8_t* row, int x, const Wide& value, int shift) {
  if (pm.GetColorType() == ColorType::kAlpha8) {
    row[x] = static_cast<std::uint8_t>(value.c[0] >> shift);
  } else {
    std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
    for (std::size_t i = 0; i < 4; ++i) {
      p[i] = static_cast<std::uint8_t>(value.c[i] >> shift);
    }
  }
}

Wide Add121(const Wide& a, const Wide& b, const Wide& c) {
  return a + b + b + c;
}

Wide ShiftLeft(const Wide& value, int shift) {
  Wide result;
  for (std::size_t i = 0; i < 4; ++i) {
    result.c[i] = value.c[i] << shift;
  }
  return result;
}

// Builds one level from the level above with the 2x2, 2x3, 3x2 and 3x3
// filters for the isotropic cases and 1x2, 1x3, 2x1 and 3x1 for the
// anisotropic ones.
void BuildLevel(const Pixmap& dst, const Pixmap& src) {
  const int width = src.Width();
  const int height = src.Height();

  // The number of source pixels sampled in each dimension.
  int sx;
  int sy;
  if (height & 1) {
    if (height == 1) {
      sy = 1;
      sx = (width & 1) ? 3 : 2;
    } else {
      sy = 3;
      sx = (width & 1) ? (width == 1 ? 1 : 3) : 2;
    }
  } else {
    sy = 2;
    sx = (width & 1) ? (width == 1 ? 1 : 3) : 2;
  }

  for (int y = 0; y < dst.Height(); y++) {
    const std::uint8_t* p0 = static_cast<const std::uint8_t*>(src.Addr()) + static_cast<std::size_t>(y) * 2 * src.RowBytes();
    const std::uint8_t* p1 = p0 + src.RowBytes();
    const std::uint8_t* p2 = p1 + src.RowBytes();
    std::uint8_t* d = static_cast<std::uint8_t*>(dst.WritableAddr()) + static_cast<std::size_t>(y) * dst.RowBytes();

    for (int i = 0; i < dst.Width(); ++i) {
      const int x = i * 2;
      Wide sum;
      int shift = 0;
      if (sx == 1 && sy == 2) {
        sum = Expand(src, p0, x) + Expand(src, p1, x);
        shift = 1;
      } else if (sx == 1 && sy == 3) {
        sum = Add121(Expand(src, p0, x), Expand(src, p1, x), Expand(src, p2, x));
        shift = 2;
      } else if (sx == 2 && sy == 1) {
        sum = Expand(src, p0, x) + Expand(src, p0, x + 1);
        shift = 1;
      } else if (sx == 2 && sy == 2) {
        sum = Expand(src, p0, x) + Expand(src, p1, x) + Expand(src, p0, x + 1) + Expand(src, p1, x + 1);
        shift = 2;
      } else if (sx == 2 && sy == 3) {
        sum = Add121(Expand(src, p0, x), Expand(src, p1, x), Expand(src, p2, x)) +
              Add121(Expand(src, p0, x + 1), Expand(src, p1, x + 1), Expand(src, p2, x + 1));
        shift = 3;
      } else if (sx == 3 && sy == 1) {
        sum = Add121(Expand(src, p0, x), Expand(src, p0, x + 1), Expand(src, p0, x + 2));
        shift = 2;
      } else if (sx == 3 && sy == 2) {
        // (a0 + 2*b0 + c0 + a1 + 2*b1 + c1) / 8
        Wide a = Expand(src, p0, x) + Expand(src, p1, x);
        Wide b0 = Expand(src, p0, x + 1);
        Wide b1 = Expand(src, p1, x + 1);
        Wide b = b0 + b0 + b1 + b1;
        Wide c = Expand(src, p0, x + 2) + Expand(src, p1, x + 2);
        sum = a + b + c;
        shift = 3;
      } else {
        // (a0 + 2*b0 + c0 + 2*a1 + 4*b1 + 2*c1 + a2 + 2*b2 + c2) / 16
        Wide a = Add121(Expand(src, p0, x), Expand(src, p1, x), Expand(src, p2, x));
        Wide b = ShiftLeft(Add121(Expand(src, p0, x + 1), Expand(src, p1, x + 1), Expand(src, p2, x + 1)), 1);
        Wide c = Add121(Expand(src, p0, x + 2), Expand(src, p1, x + 2), Expand(src, p2, x + 2));
        sum = a + b + c;
        shift = 4;
      }
      Compact(dst, d, i, sum, shift);
    }
  }
}

} // namespace

PMColor4f Pixmap::GetPMColor4f(int x, int y) const {
  const std::uint8_t* p = WritableAddr8(x, y);
  constexpr float kScale = 1 / 255.0f;
  if (color_type_ == ColorType::kAlpha8) {
    return {0, 0, 0, p[0] * kScale};
  }
  PMColor c;
  std::memcpy(&c, p, sizeof(c));
  return {static_cast<float>((c >> kR32Shift) & 0xFF) * kScale,
          static_cast<float>((c >> kG32Shift) & 0xFF) * kScale,
          static_cast<float>((c >> kB32Shift) & 0xFF) * kScale,
          static_cast<float>((c >> kA32Shift) & 0xFF) * kScale};
}

Bitmap::Bitmap(ColorType color_type, int width, int height) {
  if (width <= 0 || height <= 0) {
    return;
  }
  const std::size_t row_bytes = static_cast<std::size_t>(width) * BytesPerPixel(color_type);
  const std::size_t size = row_bytes * static_cast<std::size_t>(height);
  if (size / row_bytes != static_cast<std::size_t>(height) || size > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
    return;
  }
  storage_.assign(size, 0);
  pixmap_ = Pixmap(color_type, width, height, storage_.data(), row_bytes);
}

std::unique_ptr<Mipmap> Mipmap::Build(const Pixmap& base) {
  if (base.Width() < 1 || base.Height() < 1) {
    return nullptr;
  }
  // SkMipmap::ComputeLevelCount: the base level is not included.
  int count_levels = 0;
  {
    int largest_axis = std::max(base.Width(), base.Height());
    while (largest_axis > 1) {
      largest_axis >>= 1;
      ++count_levels;
    }
  }
  if (count_levels == 0) {
    return nullptr;
  }

  auto mipmap = std::make_unique<Mipmap>();
  // Reserved, so the source pixmap of each level stays in place.
  mipmap->levels_.reserve(static_cast<std::size_t>(count_levels));
  int width = base.Width();
  int height = base.Height();
  const Pixmap* src = &base;
  for (int i = 0; i < count_levels; ++i) {
    width = std::max(1, width >> 1);
    height = std::max(1, height >> 1);
    mipmap->levels_.emplace_back(base.GetColorType(), width, height);
    if (mipmap->levels_.back().IsEmpty()) {
      return nullptr;
    }
    BuildLevel(mipmap->levels_.back().GetPixmap(), *src);
    src = &mipmap->levels_.back().GetPixmap();
  }
  return mipmap;
}

float Mipmap::ComputeLevel(float scale_width, float scale_height) {
  // Use the smallest scale to match the GPU impl.
  const float scale = std::min(scale_width, scale_height);

  if (scale >= 1 || scale <= 0 || !std::isfinite(scale)) {
    return -1;
  }

  // The -0.5 bias here is to emulate GPU's sharpen mipmap option.
  float level = std::max(-std::log2(scale) - 0.5f, 0.f);
  if (!std::isfinite(level)) {
    return -1;
  }
  return level;
}

const Mipmap* Image::GetMipmap() const {
  if (!mipmap_built_) {
    mipmap_ = Mipmap::Build(bitmap_.GetPixmap());
    mipmap_built_ = true;
  }
  return mipmap_.get();
}

} // namespace bkit
