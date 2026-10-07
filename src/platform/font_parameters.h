// Ported from: skia/include/core/SkFontParameters.h

#pragma once

#include <cstdint>

namespace bkit {

// SkFontParameters.
struct FontParameters {
  struct Variation {
    // Parameters in a variation font axis.
    struct Axis {
      constexpr Axis()
          : tag(0),
            min(0),
            def(0),
            max(0),
            flags(0) {
      }
      constexpr Axis(std::uint32_t tag, float min, float def, float max, bool hidden)
          : tag(tag),
            min(min),
            def(def),
            max(max),
            flags(hidden ? kHidden : 0) {
      }

      // Four character identifier of the font axis (weight, width, slant,
      // italic...).
      std::uint32_t tag;
      // Minimum value supported by this axis.
      float min;
      // Default value set by this axis.
      float def;
      // Maximum value supported by this axis. The maximum can equal the
      // minimum.
      float max;
      // Return whether this axis is recommended to be remain hidden in user
      // interfaces.
      bool IsHidden() const {
        return flags & kHidden;
      }
      // Set this axis to be remain hidden in user interfaces.
      void SetHidden(bool hidden) {
        flags = static_cast<std::uint16_t>(hidden ? (flags | kHidden) : (flags & ~kHidden));
      }

    private:
      static constexpr std::uint16_t kHidden = 0x0001;
      // Attributes for a font axis.
      std::uint16_t flags;
    };
  };
};

} // namespace bkit
