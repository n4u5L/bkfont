/*
 * Copyright 2014 Google Inc.
 * Adapted from third_party/skia/src/ports/SkTypeface_win_dw.{h,cpp} and
 * SkScalerContext_win_dw.cpp. The original BSD license is retained in
 * src/platform/dwrite_internal.h.
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <span>

#include "base/once.h"
#include "base/text/wtf_string.h"
#include "base/vector.h"

namespace bkfont {

class FreeTypeFaceRec;

// The SkStreamAsset subset FreeType opens a face from.
class FontFileStream {
public:
  virtual ~FontFileStream() = default;
  virtual std::size_t GetLength() const = 0;
  // Returns null if the whole file cannot be mapped.
  virtual const void* GetMemoryBase() = 0;
};

enum class FontSlant {
  kNormal,
  kItalic,
  kOblique
};

struct FontStyle {
  int weight = 400;
  int stretch = 5;
  FontSlant slant = FontSlant::kNormal;
};

struct PlatformFontVariationAxis {
  std::uint32_t tag = 0;
  float value = 0;
};

struct FontVariationParameter {
  std::uint32_t tag = 0;
  float minimum = 0;
  float default_value = 0;
  float maximum = 0;
  bool hidden = false;
};

struct PlatformPaletteOverride {
  std::uint16_t index = 0;
  std::uint32_t color = 0; // Unpremultiplied AARRGGBB.
};

class FontSvgGlyphBoundsProvider;

struct FontRenderOptions {
  bool synthetic_bold = false;
  bool synthetic_italic = false;
  bool anti_alias = false;
  bool subpixel_rendering = false;
  bool subpixel_positioning = false;
  bool embedded_bitmaps = true;
  bool hinting = true;
  bool linear_metrics = false;
  std::shared_ptr<const FontSvgGlyphBoundsProvider> svg_bounds_provider;
  std::uint32_t foreground_color = 0xff000000u;
};

enum class FontMeasuringMode {
  kNatural,
  kGdiClassic,
  kGdiNatural
};

struct PlatformFontMetrics {
  float ascender = 0;
  float descender = 0;
  float line_gap = 0;
  float cap_height = 0;
  float x_height = 0;
  // Positions use font coordinates, positive above the baseline.
  float underline_position = 0;
  float underline_thickness = 0;
  float strikeout_position = 0;
  float strikeout_thickness = 0;
  float top = 0;
  float bottom = 0;
  float x_min = 0;
  float x_max = 0;
  float avg_char_width = 0;
  float max_char_width = 0;
  bool bounds_valid = false;
};

struct PlatformDesignGlyphMetrics {
  std::int32_t left_side_bearing = 0;
  std::uint32_t advance_width = 0;
  std::int32_t right_side_bearing = 0;
  std::int32_t top_side_bearing = 0;
  std::uint32_t advance_height = 0;
  std::int32_t bottom_side_bearing = 0;
  std::int32_t vertical_origin_y = 0;
};

struct PlatformGlyphMetrics {
  float advance_x = 0;
  float advance_y = 0;
  float left = 0;
  float top = 0;
  float width = 0;
  float height = 0;
};

struct FontGlyphTransform {
  float xx = 1;
  float yx = 0;
  float xy = 0;
  float yy = 1;
  float dx = 0;
  float dy = 0;
};

// Equivalent integration point to SkGraphics' optional OpenTypeSVGDecoder.
// A renderer can install a native SVG implementation; an absent decoder takes
// the same next-format fallback as upstream. Bounds are in transformed pixels.
class FontSvgGlyphBoundsProvider {
public:
  virtual ~FontSvgGlyphBoundsProvider() = default;
  virtual bool Measure(std::span<const std::uint8_t> svg,
                       std::uint16_t units_per_em, std::uint16_t glyph,
                       std::uint32_t foreground_color,
                       std::span<const std::uint32_t> palette,
                       const FontGlyphTransform& transform,
                       PlatformGlyphMetrics* bounds) const = 0;
};

struct LocalizedFontName {
  String name;
  String locale;
};

class FontManager;

class FontFace final : public std::enable_shared_from_this<FontFace> {
public:
  ~FontFace();
  FontFace(const FontFace&) = delete;
  FontFace& operator=(const FontFace&) = delete;

  String FamilyName() const;
  String PostScriptName() const;
  Vector<LocalizedFontName> FamilyNames() const;
  FontStyle Style() const;
  std::uint32_t UniqueId() const;
  std::uint32_t CollectionIndex() const;
  std::uint16_t UnitsPerEm() const;
  std::uint16_t GlyphCount() const;
  bool ContainsCharacter(std::uint32_t codepoint) const;
  std::uint16_t GlyphForCharacter(std::uint32_t codepoint) const;
  bool CharactersToGlyphs(std::span<const std::uint32_t> codepoints,
                          std::span<std::uint16_t> glyphs) const;
  bool IsFixedPitch() const;
  bool HasColorGlyphs() const;
  bool HasVariations() const;
  bool HasSimulations() const;

  Vector<std::uint8_t> TableData(std::uint32_t big_endian_tag) const;
  bool HasTable(std::uint32_t big_endian_tag) const;
  std::size_t ReadTable(std::uint32_t big_endian_tag, std::size_t offset,
                        std::span<std::uint8_t> destination) const;

  PlatformFontMetrics GetMetrics(
      float size, FontMeasuringMode mode = FontMeasuringMode::kNatural) const;
  bool GetDesignGlyphMetrics(std::uint16_t glyph,
                             PlatformDesignGlyphMetrics* metrics) const;
  // This box comes from DirectWrite design metrics. It is not Skia's
  // raster/outline bounds (generateDWMetrics/generatePathMetrics).
  PlatformGlyphMetrics GetGlyphMetrics(
      std::uint16_t glyph, float size, bool vertical = false,
      FontMeasuringMode mode = FontMeasuringMode::kNatural) const;

  // The SkFont measurement path: canonical size, scaler mode selection,
  // real raster/outline ink bounds, and synthetic styling. A failed native
  // bounds operation returns false; it never substitutes a design box.
  bool MeasureGlyph(std::uint16_t glyph, float size,
                    const FontRenderOptions& options,
                    PlatformGlyphMetrics* metrics) const;
  PlatformFontMetrics GetFontMetrics(float size,
                                     const FontRenderOptions& options) const;

  Vector<PlatformFontVariationAxis> VariationCoordinates() const;
  Vector<FontVariationParameter> VariationParameters() const;
  // As onMakeClone upstream: last duplicate coordinate wins; unspecified
  // coordinates retain their values. Unsupported variation APIs retain this face.
  std::shared_ptr<FontFace> WithVariations(
      std::span<const PlatformFontVariationAxis> coordinates);
  std::shared_ptr<FontFace> WithPalette(
      std::uint16_t palette_index,
      std::span<const PlatformPaletteOverride> overrides);
  Vector<std::uint32_t> PaletteColors() const;
  std::uint16_t RequestedPaletteIndex() const;

  // DWriteFontTypeface::onOpenStream. Returns null unless the face comes from
  // exactly one file.
  std::unique_ptr<FontFileStream> OpenStream(int* ttc_index) const;
  // SkTypeface_FreeType::getFaceRec. Caller must lock FreeTypeMutex() before
  // calling this function.
  FreeTypeFaceRec* GetFaceRec() const;
  // SkTypeface_FreeType::onGlyphMaskNeedsCurrentColor.
  bool GlyphMaskNeedsCurrentColor() const;

private:
  friend class FontManager;
  struct Impl;
  explicit FontFace(std::unique_ptr<Impl> implementation);
  std::unique_ptr<Impl> impl_;

  mutable Once face_rec_once_;
  mutable std::unique_ptr<FreeTypeFaceRec> face_rec_;
  mutable Once glyph_masks_may_need_current_color_once_;
  mutable bool glyph_masks_may_need_current_color_ = false;
};

} // namespace bkfont
