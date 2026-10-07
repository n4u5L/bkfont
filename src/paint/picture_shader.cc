// Ported from: skia/src/shaders/SkPictureShader.cpp
// Tile rasterization uses the local CPU canvas; see path_rasterizer.h.
#include "shader.h"

#include <algorithm>
#include <cmath>
#include <climits>
#include <mutex>

#include "picture.h"
#include "raster_canvas.h"
#include "scalar.h"

namespace bkit {
namespace {

class PictureShader final : public Shader {
public:
  PictureShader(std::shared_ptr<const Picture> picture, ScalarRect tile, TileMode x, TileMode y,
                FilterMode filter, ScalarMatrix local)
      : picture_(std::move(picture)), tile_(tile), x_(x), y_(y), filter_(filter), local_(local) {}

  std::unique_ptr<Context> MakeContext(const ScalarMatrix& ctm, const Color4f& color) const override {
    ScalarMatrix total = ctm;
    total.PreConcat(local_);
    ScalarMatrix inverse;
    if (!total.Invert(&inverse)) return nullptr;
    float sx = std::hypot(total.GetScaleX(), total.GetSkewY());
    float sy = std::hypot(total.GetSkewX(), total.GetScaleY());
    if (!(sx > kScalarNearlyZero && sy > kScalarNearlyZero)) sx = sy = 1;
    float width = sx * tile_.Width(), height = sy * tile_.Height();
    if (!std::isfinite(width) || !std::isfinite(height)) return nullptr;
    constexpr float max_area = 2048 * 2048;
    if (width * height > max_area) {
      const float scale = std::sqrt(max_area / (width * height));
      width *= scale;
      height *= scale;
    }
    if (!(width > 0 && height > 0) || width > static_cast<float>(INT_MAX / 2) || height > static_cast<float>(INT_MAX / 2)) return nullptr;
    const int w = static_cast<int>(std::ceil(width)), h = static_cast<int>(std::ceil(height));
    std::shared_ptr<const Image> image;
    {
      std::lock_guard lock(tile_mutex_);
      if (tile_image_ && tile_image_->Width() == w && tile_image_->Height() == h) image = tile_image_;
    }
    if (!image) {
      Bitmap bitmap(ColorType::kN32, w, h);
      if (bitmap.IsEmpty()) return nullptr;
      {
        RasterCanvas canvas(bitmap.GetPixmap());
        canvas.Scale(w / tile_.Width(), h / tile_.Height());
        canvas.Translate(-tile_.left, -tile_.top);
        canvas.DrawPicture(*picture_);
      }
      image = std::make_shared<Image>(std::move(bitmap));
      std::lock_guard lock(tile_mutex_);
      tile_image_ = image;
    }
    ScalarMatrix to_image = ScalarMatrix::Scale(w / tile_.Width(), h / tile_.Height());
    to_image.PreTranslate(-tile_.left, -tile_.top);
    to_image.PreConcat(inverse);
    return std::make_unique<PictureContext>(std::move(image), to_image, x_, y_, filter_, color.a);
  }

private:
  class PictureContext final : public Context {
  public:
    PictureContext(std::shared_ptr<const Image> image, ScalarMatrix inverse, TileMode x, TileMode y,
                   FilterMode filter, float alpha)
        : image_(std::move(image)), inverse_(inverse), x_(x), y_(y), filter_(filter), alpha_(alpha) {}
    void ShadeSpan(int x, int y, int count, PMColor4f* out) override {
      for (int i = 0; i < count; ++i) {
        const ScalarPoint p = inverse_.MapPoint({x + i + 0.5f, y + 0.5f});
        PMColor4f c;
        if (std::isfinite(p.x) && std::isfinite(p.y)) {
          if (filter_ == FilterMode::kNearest) {
            c = Pixel(std::floor(p.x), std::floor(p.y));
          } else {
            const float ix = std::floor(p.x - 0.5f), iy = std::floor(p.y - 0.5f);
            const float fx = p.x - 0.5f - ix, fy = p.y - 0.5f - iy;
            const auto a = Pixel(ix, iy), b = Pixel(ix + 1, iy), d = Pixel(ix, iy + 1), e = Pixel(ix + 1, iy + 1);
            const auto mix = [](PMColor4f a, PMColor4f b, float t) {
              return PMColor4f{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                               a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
            };
            c = mix(mix(a, b, fx), mix(d, e, fx), fy);
          }
        }
        out[i] = c * alpha_;
      }
    }
  private:
    static int Tile(float p, int size, TileMode mode) {
      switch (mode) {
      case TileMode::kDecal:
        if (p < 0 || p >= size) return -1;
        break;
      case TileMode::kClamp:
        p = std::clamp(p, 0.0f, static_cast<float>(size - 1));
        break;
      case TileMode::kRepeat:
        p = std::fmod(p, static_cast<float>(size));
        if (p < 0) p += size;
        break;
      case TileMode::kMirror:
        p = std::fmod(p, 2.0f * size);
        if (p < 0) p += 2 * size;
        if (p >= size) p = 2 * size - 1 - p;
        break;
      }
      return std::clamp(static_cast<int>(p), 0, size - 1);
    }
    PMColor4f Pixel(float x, float y) const {
      const int ix = Tile(x, image_->Width(), x_), iy = Tile(y, image_->Height(), y_);
      return ix < 0 || iy < 0 ? PMColor4f{} : image_->GetPixmap().GetPMColor4f(ix, iy);
    }
    std::shared_ptr<const Image> image_;
    ScalarMatrix inverse_;
    TileMode x_, y_;
    FilterMode filter_;
    float alpha_;
  };
  std::shared_ptr<const Picture> picture_;
  ScalarRect tile_;
  TileMode x_, y_;
  FilterMode filter_;
  ScalarMatrix local_;
  // Tile rasterization depends on the picture, tile and raster dimensions,
  // not the device translation or the paint alpha. Retain one size per
  // shader; contexts own their images if a later draw replaces this entry.
  mutable std::mutex tile_mutex_;
  mutable std::shared_ptr<const Image> tile_image_;
};
} // namespace

std::shared_ptr<const Shader> MakePictureShader(std::shared_ptr<const Picture> picture, const ScalarRect& tile,
                                                TileMode x, TileMode y, FilterMode filter, const ScalarMatrix& local) {
  ScalarMatrix inverse;
  if (!local.Invert(&inverse)) return nullptr;
  if (!picture || picture->CullRect().IsEmpty() || tile.IsEmpty()) return MakeEmptyShader();
  return std::make_shared<PictureShader>(std::move(picture), tile, x, y, filter, local);
}
} // namespace bkit
