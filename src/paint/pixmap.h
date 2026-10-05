// Ported from: skia/include/core/SkPixmap.h
// Ported from: skia/include/core/SkBitmap.h
// Ported from: skia/src/core/SkMipmap.h

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "color4f.h"
#include "rect.h"

namespace bkfont {

// SkColorType, reduced to the two glyph image formats: kAlpha_8 and the
// premultiplied kN32 (B, G, R, A bytes; see PMColor).
enum class ColorType : std::uint8_t {
  kAlpha8,
  kN32,
};

inline std::size_t BytesPerPixel(ColorType color_type) {
  return color_type == ColorType::kAlpha8 ? 1 : 4;
}

// SkPixmap. The pixels are not owned.
class Pixmap {
public:
  Pixmap() = default;
  Pixmap(ColorType color_type, int width, int height, void* pixels, std::size_t row_bytes)
      : color_type_(color_type), width_(width), height_(height), pixels_(pixels), row_bytes_(row_bytes) {
  }

  ColorType GetColorType() const {
    return color_type_;
  }
  int Width() const {
    return width_;
  }
  int Height() const {
    return height_;
  }
  IntRect Bounds() const {
    return IntRect::MakeWH(width_, height_);
  }
  std::size_t RowBytes() const {
    return row_bytes_;
  }
  void* WritableAddr() const {
    return pixels_;
  }
  const void* Addr() const {
    return pixels_;
  }
  std::uint8_t* WritableAddr8(int x, int y) const {
    return static_cast<std::uint8_t*>(pixels_) + static_cast<std::size_t>(y) * row_bytes_ + static_cast<std::size_t>(x) * BytesPerPixel(color_type_);
  }

  // Reads one pixel as a premultiplied color. Alpha-only pixels are black.
  PMColor4f GetPMColor4f(int x, int y) const;

private:
  ColorType color_type_ = ColorType::kAlpha8;
  int width_ = 0;
  int height_ = 0;
  void* pixels_ = nullptr;
  std::size_t row_bytes_ = 0;
};

// SkBitmap with its own tightly packed pixel storage.
class Bitmap {
public:
  Bitmap() = default;
  // Allocates zeroed pixels. Returns an empty bitmap for invalid sizes.
  Bitmap(ColorType color_type, int width, int height);

  // The pixmap points into the storage, which keeps its buffer when moved.
  Bitmap(const Bitmap&) = delete;
  Bitmap& operator=(const Bitmap&) = delete;
  Bitmap(Bitmap&&) = default;
  Bitmap& operator=(Bitmap&&) = default;

  bool IsEmpty() const {
    return pixmap_.Width() <= 0 || pixmap_.Height() <= 0;
  }
  const Pixmap& GetPixmap() const {
    return pixmap_;
  }

private:
  std::vector<std::uint8_t> storage_;
  Pixmap pixmap_;
};

// SkMipmap built with the HQ down sampler. Level i has the size of the base
// halved i + 1 times, never smaller than 1.
class Mipmap {
public:
  static std::unique_ptr<Mipmap> Build(const Pixmap& base);

  int CountLevels() const {
    return static_cast<int>(levels_.size());
  }
  const Pixmap& GetLevel(int index) const {
    return levels_[static_cast<std::size_t>(index)].GetPixmap();
  }

  // SkMipmap::ComputeLevel. Returns a negative value when no mip level
  // should be used.
  static float ComputeLevel(float scale_width, float scale_height);

private:
  std::vector<Bitmap> levels_;
};

// SkImage backed by a copy of the pixels, with lazily built mipmaps.
class Image {
public:
  explicit Image(Bitmap bitmap)
      : bitmap_(std::move(bitmap)) {
  }

  int Width() const {
    return bitmap_.GetPixmap().Width();
  }
  int Height() const {
    return bitmap_.GetPixmap().Height();
  }
  const Pixmap& GetPixmap() const {
    return bitmap_.GetPixmap();
  }
  bool IsAlphaOnly() const {
    return bitmap_.GetPixmap().GetColorType() == ColorType::kAlpha8;
  }

  // Not thread safe; images belong to a single drawing operation.
  const Mipmap* GetMipmap() const;

private:
  Bitmap bitmap_;
  mutable std::unique_ptr<Mipmap> mipmap_;
  mutable bool mipmap_built_ = false;
};

} // namespace bkfont
