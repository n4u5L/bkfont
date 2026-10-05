// Subset of core/css/css_to_length_conversion_data.h and
// css_length_resolver.h for the ported units: absolute lengths, the font- and
// line-height-relative units, and angles. No viewport, container or anchor
// data: the local model has none.
//
// As upstream, font-relative units scale the unzoomed specified font size by
// the requested zoom, and font-metrics-based units unzoom the (zoomed) font
// metrics by the zoom of their font before applying the requested zoom.
#pragma once

#include <optional>

#include "font/font.h"
#include "geometry/length.h"
#include "style/css_primitive_value.h"

namespace bkfont {

class ComputedStyle;
class ComputedStyleBuilder;

// core/style/font_size_style.h: the font inputs of length conversion.
class FontSizeStyle {
public:
  FontSizeStyle(const Font& font, const Length& specified_line_height, float effective_zoom)
      : font_(font), specified_line_height_(specified_line_height), effective_zoom_(effective_zoom) {}
  const Font& GetFont() const { return font_; }
  float SpecifiedFontSize() const { return font_.GetFontDescription().SpecifiedSize(); }
  const Length& SpecifiedLineHeight() const { return specified_line_height_; }
  float EffectiveZoom() const { return effective_zoom_; }

private:
  const Font& font_;
  const Length& specified_line_height_;
  float effective_zoom_;
};

// A value snapshot for one application phase. Fonts are copied, so the data
// never borrows storage from a mutable builder.
class CSSToLengthConversionData {
public:
  class FontSizes {
  public:
    FontSizes() = default;
    FontSizes(float em, float rem, const Font& font, const Font& root_font, float font_zoom, float root_font_zoom)
        : em_(em), rem_(rem), font_(font), root_font_(root_font), font_zoom_(font_zoom),
          root_font_zoom_(root_font_zoom) {}
    // `root_style` is null for the root element, which uses its own font.
    FontSizes(const FontSizeStyle&, const ComputedStyle* root_style);

    float Em(float zoom) const { return em_ * zoom; }
    float Rem(float zoom) const { return rem_ * zoom; }
    float Ex(float zoom) const;
    float Rex(float zoom) const;
    float Ch(float zoom) const;
    float Rch(float zoom) const;
    float Ic(float zoom) const;
    float Ric(float zoom) const;
    float Cap(float zoom) const;
    float Rcap(float zoom) const;

  private:
    float em_ = 0;
    float rem_ = 0;
    std::optional<Font> font_;
    std::optional<Font> root_font_;
    // Font-metrics-based units are pre-zoomed by a factor of `font_zoom_`.
    float font_zoom_ = 1;
    float root_font_zoom_ = 1;
  };

  class LineHeightSize {
  public:
    LineHeightSize() = default;
    LineHeightSize(const Length& line_height, const Length& root_line_height, const Font& font,
                   const Font& root_font, float font_zoom, float root_font_zoom)
        : line_height_(line_height), root_line_height_(root_line_height), font_(font), root_font_(root_font),
          font_zoom_(font_zoom), root_font_zoom_(root_font_zoom) {}
    LineHeightSize(const FontSizeStyle&, const ComputedStyle* root_style);

    float Lh(float zoom) const;
    float Rlh(float zoom) const;

  private:
    Length line_height_;
    Length root_line_height_;
    std::optional<Font> font_;
    std::optional<Font> root_font_;
    float font_zoom_ = 1;
    float root_font_zoom_ = 1;
  };

  CSSToLengthConversionData() = default;
  CSSToLengthConversionData(const FontSizes&, const LineHeightSize&, float zoom);

  float Zoom() const { return zoom_; }
  void SetFontSizes(const FontSizes& font_sizes) { font_sizes_ = font_sizes; }
  void SetLineHeightSize(const LineHeightSize& line_height_size) { line_height_size_ = line_height_size; }
  void SetZoom(float zoom);
  CSSToLengthConversionData CopyWithAdjustedZoom(float new_zoom) const {
    return CSSToLengthConversionData(font_sizes_, line_height_size_, new_zoom);
  }
  CSSToLengthConversionData Unzoomed() const { return CopyWithAdjustedZoom(1.0f); }

  float EmFontSize(float zoom) const { return font_sizes_.Em(zoom); }
  float RemFontSize(float zoom) const { return font_sizes_.Rem(zoom); }
  float ExFontSize(float zoom) const { return font_sizes_.Ex(zoom); }
  float RexFontSize(float zoom) const { return font_sizes_.Rex(zoom); }
  float ChFontSize(float zoom) const { return font_sizes_.Ch(zoom); }
  float RchFontSize(float zoom) const { return font_sizes_.Rch(zoom); }
  float IcFontSize(float zoom) const { return font_sizes_.Ic(zoom); }
  float RicFontSize(float zoom) const { return font_sizes_.Ric(zoom); }
  float CapFontSize(float zoom) const { return font_sizes_.Cap(zoom); }
  float RcapFontSize(float zoom) const { return font_sizes_.Rcap(zoom); }
  float LineHeight(float zoom) const { return line_height_size_.Lh(zoom); }
  float RootLineHeight(float zoom) const { return line_height_size_.Rlh(zoom); }

  // CSSLengthResolver::ZoomedComputedPixels(): `value` in `unit`, in zoomed
  // pixels. Numbers, percentages and angles have no length and resolve to 0.
  double ZoomedComputedPixels(double value, CSSPrimitiveValue::UnitType unit) const;

private:
  FontSizes font_sizes_;
  LineHeightSize line_height_size_;
  float zoom_ = 1;
};

} // namespace bkfont
