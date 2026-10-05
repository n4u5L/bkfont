// Ported from: skia/src/core/SkGlyph.h

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "paint/mask.h"
#include "paint/path.h"
#include "paint/rect.h"

namespace bkfont {

class Arena;
class Drawable;
class PlatformGlyph;
class ScalerContext;
class Strike;

// SkFixed.
using Fixed = std::int32_t;

// SkPackedGlyphID. A glyph id with a 2-bit subpixel position in each axis.
struct PackedGlyphID {
  inline static constexpr std::uint32_t kImpossibleID = ~0u;
  enum : std::uint32_t {
    // Lengths
    kGlyphIDLen = 16u,
    kSubPixelPosLen = 2u,

    // Bit positions
    kSubPixelX = 0u,
    kGlyphID = kSubPixelPosLen,
    kSubPixelY = kGlyphIDLen + kSubPixelPosLen,
    kEndData = kGlyphIDLen + 2 * kSubPixelPosLen,

    // Masks
    kGlyphIDMask = (1u << kGlyphIDLen) - 1,
    kSubPixelPosMask = (1u << kSubPixelPosLen) - 1,
    kMaskAll = (1u << kEndData) - 1,

    // Location of sub pixel info in a fixed pointer number.
    kFixedPointBinaryPointPos = 16u,
    kFixedPointSubPixelPosBits = kFixedPointBinaryPointPos - kSubPixelPosLen,
  };

  inline static constexpr float kSubpixelRound = 1.f / (1u << (kSubPixelPosLen + 1));

  // kXYFieldMask, as the x and y of an SkIPoint.
  inline static constexpr std::int32_t kXFieldMask = kSubPixelPosMask << kSubPixelX;
  inline static constexpr std::int32_t kYFieldMask = kSubPixelPosMask << kSubPixelY;

  struct Hash {
    std::size_t operator()(PackedGlyphID packed_id) const {
      return packed_id.Hash32();
    }
  };

  constexpr explicit PackedGlyphID(std::uint16_t glyph_id)
      : id_{static_cast<std::uint32_t>(glyph_id) << kGlyphID} {
  }

  constexpr PackedGlyphID(std::uint16_t glyph_id, Fixed x, Fixed y)
      : id_{PackIDXY(glyph_id, x, y)} {
  }

  constexpr PackedGlyphID(std::uint16_t glyph_id, std::uint32_t x, std::uint32_t y)
      : id_{PackIDSubXSubY(glyph_id, x, y)} {
  }

  // mask_x and mask_y are the SkIPoint mask, normally the
  // ignore_position_field_mask of a GlyphPositionRoundingSpec.
  PackedGlyphID(std::uint16_t glyph_id, ScalarPoint pt, std::int32_t mask_x, std::int32_t mask_y)
      : id_{PackIDPoint(glyph_id, pt, mask_x, mask_y)} {
  }

  constexpr explicit PackedGlyphID(std::uint32_t v)
      : id_{v & kMaskAll} {
  }
  constexpr PackedGlyphID()
      : id_{kImpossibleID} {
  }

  bool operator==(const PackedGlyphID& that) const {
    return id_ == that.id_;
  }
  bool operator!=(const PackedGlyphID& that) const {
    return !(*this == that);
  }
  bool operator<(PackedGlyphID that) const {
    return id_ < that.id_;
  }

  std::uint16_t GlyphID() const {
    return static_cast<std::uint16_t>((id_ >> kGlyphID) & kGlyphIDMask);
  }

  std::uint32_t Value() const {
    return id_;
  }

  Fixed GetSubXFixed() const {
    return SubToFixed(kSubPixelX);
  }

  Fixed GetSubYFixed() const {
    return SubToFixed(kSubPixelY);
  }

  // SkChecksum::CheapMix.
  std::uint32_t Hash32() const {
    std::uint32_t hash = id_;
    hash ^= hash >> 16;
    hash *= 0x85ebca6b;
    hash ^= hash >> 16;
    return hash;
  }

private:
  static constexpr std::uint32_t PackIDSubXSubY(std::uint16_t glyph_id, std::uint32_t x, std::uint32_t y) {
    return (x << kSubPixelX) | (y << kSubPixelY) | (static_cast<std::uint32_t>(glyph_id) << kGlyphID);
  }

  // Assumptions: pt is properly rounded. mask is set for the x or y fields.
  //
  // A sub-pixel field is a number on the interval [2^kSubPixel, 2^(kSubPixel
  // + kSubPixelPosLen)). Where kSubPixel is either kSubPixelX or kSubPixelY.
  // Given a number x on [0, 1) we can generate a sub-pixel field using:
  //   sub-pixel-field = x * 2^(kSubPixel + kSubPixelPosLen)
  //
  // There is no single mask that can be used to generate the sub-pixel field
  // value with all-pixel position value, so x and y are done separately.
  static std::uint32_t PackIDPoint(std::uint16_t glyph_id, ScalarPoint pt, std::int32_t mask_x, std::int32_t mask_y) {
    const float magic_x = 1.f * (1u << (kSubPixelPosLen + kSubPixelX));
    const float magic_y = 1.f * (1u << (kSubPixelPosLen + kSubPixelY));
    float x = pt.x;
    float y = pt.y;
    x = (x - std::floor(x)) + 1.0f;
    y = (y - std::floor(y)) + 1.0f;
    const int sub[] = {
        static_cast<int>(x * magic_x) & mask_x,
        static_cast<int>(y * magic_y) & mask_y,
    };
    return (static_cast<std::uint32_t>(glyph_id) << kGlyphID) | static_cast<std::uint32_t>(sub[0]) | static_cast<std::uint32_t>(sub[1]);
  }

  static constexpr std::uint32_t PackIDXY(std::uint16_t glyph_id, Fixed x, Fixed y) {
    return PackIDSubXSubY(glyph_id, FixedToSub(x), FixedToSub(y));
  }

  static constexpr std::uint32_t FixedToSub(Fixed n) {
    return (static_cast<std::uint32_t>(n) >> kFixedPointSubPixelPosBits) & kSubPixelPosMask;
  }

  constexpr std::uint32_t SubPixelField(std::uint32_t sub_pixel_pos_bit) const {
    return (id_ >> sub_pixel_pos_bit) & kSubPixelPosMask;
  }

  constexpr Fixed SubToFixed(std::uint32_t sub_pixel_pos_bit) const {
    const std::uint32_t sub_pixel_position = SubPixelField(sub_pixel_pos_bit);
    return static_cast<Fixed>(sub_pixel_position << kFixedPointSubPixelPosBits);
  }

  std::uint32_t id_;
};

// SkAxisAlignment.
enum class AxisAlignment : std::uint32_t {
  kNone, // No x or y axis alignment.
  kX,    // Snap the y axis and allow full movement in the x axis.
  kY,    // Snap the x axis and allow full movement in the y axis.
};

// SkGlyphPositionRoundingSpec. Captures the information for rounding glyph
// positions. The SkIPoint masks are stored as their x and y components.
struct GlyphPositionRoundingSpec {
  GlyphPositionRoundingSpec(bool is_subpixel, AxisAlignment axis_alignment);

  const ScalarPoint half_axis_sample_freq;
  const std::int32_t ignore_position_mask_x;
  const std::int32_t ignore_position_mask_y;
  const std::int32_t ignore_position_field_mask_x;
  const std::int32_t ignore_position_field_mask_y;
};

// skglyph::GlyphAction.
enum class GlyphAction {
  kUnset,
  kAccept,
  kReject,
  kDrop,
  kSize,
};

// skglyph::ActionType, the bit offset of each action in the digest.
enum GlyphActionType {
  kDirectMask = 0,
  kDirectMaskCPU = 2,
  kMask = 4,
  kSDFT = 6,
  kPath = 8,
  kDrawable = 10,
};

// SkGlyphDigest. A compact summary of a glyph and the actions decided for it.
class GlyphDigest {
public:
  // An atlas consists of plots, and plots hold glyphs. The minimum a plot can
  // be is 256x256. This means that the maximum size a glyph can be is
  // 256x256.
  static constexpr std::uint16_t kSkSideTooBigForAtlas = 256;

  GlyphDigest() = default;
  GlyphDigest(std::size_t index, const PlatformGlyph& glyph);

  int Index() const {
    return static_cast<int>(index_);
  }
  bool IsEmpty() const {
    return is_empty_;
  }
  bool IsColor() const {
    return format_ == MaskFormat::kARGB32;
  }
  MaskFormat GetMaskFormat() const {
    return format_;
  }

  GlyphAction ActionFor(GlyphActionType action_type) const {
    return static_cast<GlyphAction>((actions_ >> action_type) & 0b11);
  }

  void SetActionFor(GlyphActionType action_type, PlatformGlyph* glyph, Strike* strike);

  std::uint16_t MaxDimension() const {
    return std::max(width_, height_);
  }

  bool FitsInAtlasDirect() const {
    return MaxDimension() <= kSkSideTooBigForAtlas;
  }

  bool FitsInAtlasInterpolated() const {
    // This is the only place in Skia that uses the interpolated atlas size.
    // The two pixel padding is for the bilerp sampling border.
    return MaxDimension() <= kSkSideTooBigForAtlas - 2;
  }

  // SkGlyphRect bounds, as a plain rect.
  ScalarRect Bounds() const {
    return ScalarRect::MakeLTRB(left_, top_, static_cast<float>(left_) + width_, static_cast<float>(top_) + height_);
  }

  static bool FitsInAtlas(const PlatformGlyph& glyph);

  PackedGlyphID GetPackedID() const {
    return PackedGlyphID{packed_id_};
  }

private:
  void SetAction(GlyphActionType action_type, GlyphAction action) {
    const std::uint32_t mask = 0b11u << action_type;
    actions_ &= ~mask;
    actions_ |= static_cast<std::uint32_t>(action) << action_type;
  }

  // The bit fields are stored as plain members.
  std::uint32_t packed_id_ = PackedGlyphID::kImpossibleID;
  std::uint32_t index_ = 0;
  bool is_empty_ = true;
  MaskFormat format_ = MaskFormat::kBW;
  std::uint32_t actions_ = 0;
  std::int16_t left_ = 0;
  std::int16_t top_ = 0;
  std::uint16_t width_ = 0;
  std::uint16_t height_ = 0;
};

// SkGlyph. Images, path data and drawable data are owned by the Arena of the
// Strike that made the glyph. Serialization for remote glyph caches is not
// ported.
class PlatformGlyph {
public:
  PlatformGlyph()
      : PlatformGlyph{PackedGlyphID()} {
  }
  explicit PlatformGlyph(PackedGlyphID id)
      : id_{id} {
  }
  explicit PlatformGlyph(std::uint16_t glyph_id)
      : id_{PackedGlyphID{glyph_id}} {
  }

  ScalarPoint AdvanceVector() const {
    return {advance_x_, advance_y_};
  }
  float AdvanceX() const {
    return advance_x_;
  }
  float AdvanceY() const {
    return advance_y_;
  }

  std::uint16_t GetGlyphID() const {
    return id_.GlyphID();
  }
  PackedGlyphID GetPackedID() const {
    return id_;
  }
  Fixed GetSubXFixed() const {
    return id_.GetSubXFixed();
  }
  Fixed GetSubYFixed() const {
    return id_.GetSubYFixed();
  }

  std::size_t RowBytes() const;
  std::size_t RowBytesUsingFormat(MaskFormat format) const;

  // Call this to set all the metrics fields to 0 (e.g. if the scaler
  // encounters an error measuring a glyph). Note: this does not alter the
  // image or path.
  void ZeroMetrics();

  Mask GetMask() const;

  // position must be integral; the bounds are offset by its floor.
  Mask GetMask(ScalarPoint position) const;

  // If we haven't already tried to associate an image with this glyph (i.e.
  // SetImageHasBeenCalled() returns false), then use the ScalerContext to set
  // the image.
  bool SetImage(Arena* arena, ScalerContext* scaler_context);

  // If SetImage has not been called, copy the image from image.
  bool SetImage(Arena* arena, const void* image);

  // Returns true if the image has been set.
  bool SetImageHasBeenCalled() const {
    // An empty image can have a nullptr image, but still have a set image.
    return IsEmpty() || image_ != nullptr || ImageTooLarge();
  }

  // Return a pointer to the image. Valid only after SetImage() is called.
  const void* Image() const {
    return image_;
  }

  // Return the size of the image.
  std::size_t ImageSize() const;

  // If we haven't already tried to associate a path to this glyph (i.e.
  // SetPathHasBeenCalled() returns false), then use the ScalerContext to set
  // the path. Returns true if a path was set.
  bool SetPath(Arena* arena, ScalerContext* scaler_context);

  // If we haven't already tried to associate a path to this glyph, set it to
  // path. A null path means the glyph has no path. Returns true if a path was
  // set.
  bool SetPath(Arena* arena, const ScalarPath* path, bool hairline, bool modified);

  // Returns true if that path has been set. The path may be null which
  // indicates that there is no path.
  bool SetPathHasBeenCalled() const {
    return path_data_ != nullptr;
  }

  // Return a pointer to the path if it exists, otherwise return nullptr. Only
  // works if the path was previously set.
  const ScalarPath* Path() const;
  bool PathIsHairline() const;
  bool PathIsModified() const;

  // If we haven't already tried to associate a drawable to this glyph (i.e.
  // SetDrawableHasBeenCalled() returns false), then use the ScalerContext to
  // set the drawable. Returns true if a drawable was set.
  bool SetDrawable(Arena* arena, ScalerContext* scaler_context);

  // If we haven't already tried to associate a drawable to this glyph, set
  // it to drawable. Returns true if a drawable was set.
  bool SetDrawable(Arena* arena, std::shared_ptr<Drawable> drawable);

  // Returns true if that drawable has been set. The drawable may be null
  // which indicates that there is no drawable.
  bool SetDrawableHasBeenCalled() const {
    return drawable_data_ != nullptr;
  }

  // Return a pointer to the drawable if it exists, otherwise return nullptr.
  // Only works if the drawable was previously set.
  Drawable* GetDrawable() const;

  bool IsColor() const {
    return mask_format_ == MaskFormat::kARGB32;
  }
  MaskFormat GetMaskFormat() const {
    return mask_format_;
  }
  std::size_t FormatAlignment() const;

  int MaxDimension() const {
    return std::max(width_, height_);
  }
  IntRect IRect() const {
    return IntRect::MakeXYWH(left_, top_, width_, height_);
  }
  ScalarRect Rect() const {
    return ScalarRect::MakeXYWH(left_, top_, width_, height_);
  }
  int Left() const {
    return left_;
  }
  int Top() const {
    return top_;
  }
  int Width() const {
    return width_;
  }
  int Height() const {
    return height_;
  }
  bool IsEmpty() const {
    // height_ == 0 -> width_ == 0;
    return width_ == 0 || height_ == 0;
  }
  bool ImageTooLarge() const {
    return width_ >= kMaxGlyphWidth;
  }

  std::uint16_t ExtraBits() const {
    return scaler_context_bits_;
  }

  // Make sure that the intercept information is on the glyph and return it,
  // or return it if it already exists.
  // * bounds - [0] - top of underline; [1] - bottom of underline.
  // * scale, x_pos - information about how wide the gap is.
  // * array - accumulated gaps for many characters if not null.
  // * count - the number of gaps.
  void EnsureIntercepts(const float bounds[2], float scale, float x_pos,
                        float* array, int* count, Arena* arena);

private:
  friend class ScalerContext;

  inline static constexpr std::uint16_t kMaxGlyphWidth = 1u << 13u;

  struct Intercept {
    Intercept* next;
    float bounds[2];   // for horz underlines, the boundaries in Y
    float interval[2]; // the outside intersections of the axis and the glyph
  };

  struct PathData {
    Intercept* intercept{nullptr};
    ScalarPath path;
    bool has_path{false};
    bool hairline{false};
    bool modified{false};
  };

  struct DrawableData {
    Intercept* intercept{nullptr};
    std::shared_ptr<Drawable> drawable;
    bool has_drawable{false};
  };

  std::size_t AllocImage(Arena* arena);

  void InstallPath(Arena* arena, const ScalarPath* path, bool hairline, bool modified);
  void InstallDrawable(Arena* arena, std::shared_ptr<Drawable> drawable);

  // The width and height of the glyph mask.
  std::uint16_t width_ = 0;
  std::uint16_t height_ = 0;

  // The offset from the glyphs origin on the baseline to the top left of the
  // glyph mask.
  std::int16_t top_ = 0;
  std::int16_t left_ = 0;

  // image_ must remain null if the glyph is empty or if width >
  // kMaxGlyphWidth.
  void* image_ = nullptr;

  // Path data has tricky state. If the glyph IsEmpty(), then path_data_
  // should always be nullptr (unless SetPath was called with a null path). If
  // path_data_ is not null, then the state can be determined using the
  // following logic.
  // * has_path is false - an attempt was made to get a path, but there is no
  //   path; the glyph has no outline.
  // * has_path is true - the path exists.
  PathData* path_data_ = nullptr;
  DrawableData* drawable_data_ = nullptr;

  // The advance for this glyph.
  float advance_x_ = 0;
  float advance_y_ = 0;

  MaskFormat mask_format_ = MaskFormat::kBW;

  // Used by the ScalerContext to indicate what kind of glyph this is.
  std::uint16_t scaler_context_bits_ = 0;

  // The packed glyph id, which contains the glyph id and its subpixel
  // position.
  PackedGlyphID id_;
};

} // namespace bkfont
