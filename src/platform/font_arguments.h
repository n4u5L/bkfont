// Ported from: skia/include/core/SkFontArguments.h

#pragma once

#include <cstdint>

namespace bkit {

// SkFontArguments. Represents a set of actual arguments for a font.
struct FontArguments {
  struct VariationPosition {
    struct Coordinate {
      std::uint32_t axis;
      float value;
    };
    const Coordinate* coordinates;
    int coordinate_count;
  };

  // Specify a palette to use and overrides for palette entries.
  //
  // `overrides` is a list of pairs of palette entry index and color. The
  // overriden palette entries will use the associated color. Override pairs
  // with palette entry indices out of range will not be applied. Later
  // override entries override earlier ones.
  struct Palette {
    struct Override {
      std::uint16_t index;
      // SkColor: unpremultiplied ARGB.
      std::uint32_t color;
    };
    int index;
    const Override* overrides;
    int override_count;
  };

  FontArguments()
      : collection_index_(0),
        variation_design_position_{nullptr, 0},
        palette_{0, nullptr, 0} {
  }

  // Specify the index of the desired font.
  //
  // Font formats like ttc, dfont, cff, cid, pfr, t42, t1, and fon may actually
  // be indexed collections of fonts.
  FontArguments& SetCollectionIndex(int collection_index) {
    collection_index_ = collection_index;
    return *this;
  }

  // Specify a position in the variation design space.
  //
  // Any axis not specified will use the default value when creating a font,
  // or the current value when cloning a typeface.
  // Any specified axis not actually present in the font will be ignored.
  //
  // @param position not copied. The value must remain valid for life of
  // FontArguments.
  FontArguments& SetVariationDesignPosition(VariationPosition position) {
    variation_design_position_.coordinates = position.coordinates;
    variation_design_position_.coordinate_count = position.coordinate_count;
    return *this;
  }

  int GetCollectionIndex() const {
    return collection_index_;
  }

  VariationPosition GetVariationDesignPosition() const {
    return variation_design_position_;
  }

  // FreeType applies this palette on every clone. A default FontArguments
  // supplies palette zero with no overrides; retaining a palette requires
  // supplying it again, unlike omitted variation axes.
  FontArguments& SetPalette(Palette palette) {
    palette_.index = palette.index;
    palette_.overrides = palette.overrides;
    palette_.override_count = palette.override_count;
    return *this;
  }

  Palette GetPalette() const {
    return palette_;
  }

private:
  int collection_index_;
  VariationPosition variation_design_position_;
  Palette palette_;
};

} // namespace bkit
