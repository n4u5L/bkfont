// Ported from: skia/include/core/SkFontStyle.h

#pragma once

#include <algorithm>
#include <cstdint>

namespace bkit {

// SkFontStyle.
class FontStyle {
public:
  enum Weight {
    kInvisible_Weight = 0,
    kThin_Weight = 100,
    kExtraLight_Weight = 200,
    kLight_Weight = 300,
    kNormal_Weight = 400,
    kMedium_Weight = 500,
    kSemiBold_Weight = 600,
    kBold_Weight = 700,
    kExtraBold_Weight = 800,
    kBlack_Weight = 900,
    kExtraBlack_Weight = 1000,
  };

  enum Width {
    kUltraCondensed_Width = 1,
    kExtraCondensed_Width = 2,
    kCondensed_Width = 3,
    kSemiCondensed_Width = 4,
    kNormal_Width = 5,
    kSemiExpanded_Width = 6,
    kExpanded_Width = 7,
    kExtraExpanded_Width = 8,
    kUltraExpanded_Width = 9,
  };

  enum Slant {
    kUpright_Slant,
    kItalic_Slant,
    kOblique_Slant,
  };

  constexpr FontStyle(int weight, int width, Slant slant)
      : value_((std::clamp<int>(weight, kInvisible_Weight, kExtraBlack_Weight)) + (std::clamp<int>(width, kUltraCondensed_Width, kUltraExpanded_Width) << 16) + (std::clamp<int>(slant, kUpright_Slant, kOblique_Slant) << 24)) {
  }

  constexpr FontStyle()
      : FontStyle{kNormal_Weight, kNormal_Width, kUpright_Slant} {
  }

  bool operator==(const FontStyle& rhs) const {
    return value_ == rhs.value_;
  }

  int GetWeight() const {
    return value_ & 0xFFFF;
  }
  int GetWidth() const {
    return (value_ >> 16) & 0xFF;
  }
  Slant GetSlant() const {
    return static_cast<Slant>((value_ >> 24) & 0xFF);
  }

  static constexpr FontStyle Normal() {
    return FontStyle(kNormal_Weight, kNormal_Width, kUpright_Slant);
  }
  static constexpr FontStyle Bold() {
    return FontStyle(kBold_Weight, kNormal_Width, kUpright_Slant);
  }
  static constexpr FontStyle Italic() {
    return FontStyle(kNormal_Weight, kNormal_Width, kItalic_Slant);
  }
  static constexpr FontStyle BoldItalic() {
    return FontStyle(kBold_Weight, kNormal_Width, kItalic_Slant);
  }

private:
  std::int32_t value_;
};

} // namespace bkit
