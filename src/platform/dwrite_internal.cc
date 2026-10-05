// Ported from: skia/src/utils/win/SkDWrite.cpp

#include "dwrite_internal.h"

#include "base/vector.h"
#include "font_descriptor.h"

namespace bkfont {

String DWriteLocalizedString(IDWriteLocalizedStrings* names,
                             const wchar_t* prefered_locale) {
  UINT32 name_index = 0;
  if (prefered_locale) {
    // Ignore any errors and continue with index 0 if there is a problem.
    BOOL name_exists = FALSE;
    (void)names->FindLocaleName(prefered_locale, &name_index, &name_exists);
    if (!name_exists) {
      name_index = 0;
    }
  }

  UINT32 name_len;
  if (FAILED(names->GetStringLength(name_index, &name_len))) {
    return String();
  }

  std::unique_ptr<wchar_t[]> name = std::make_unique<wchar_t[]>(static_cast<std::size_t>(name_len) + 1);
  if (FAILED(names->GetString(name_index, name.get(), name_len + 1))) {
    return String();
  }

  return FromWide(name.get(), name_len);
}

// DWriteFontTypeface::GetStyle from skia/src/ports/SkTypeface_win_dw.cpp.
FontStyle DWriteFontStyle(IDWriteFont* font, IDWriteFontFace* font_face) {
  int weight = font->GetWeight();
  int width = font->GetStretch();
  FontStyle::Slant slant = FontStyle::kUpright_Slant;
  switch (font->GetStyle()) {
  case DWRITE_FONT_STYLE_NORMAL:
    slant = FontStyle::kUpright_Slant;
    break;
  case DWRITE_FONT_STYLE_OBLIQUE:
    slant = FontStyle::kOblique_Slant;
    break;
  case DWRITE_FONT_STYLE_ITALIC:
    slant = FontStyle::kItalic_Slant;
    break;
  default:
    break;
  }

#if defined(NTDDI_WIN10_RS3) && NTDDI_VERSION >= NTDDI_WIN10_RS3
  [&weight, &width, &slant, font_face]() -> void {
    ComPtr<IDWriteFontFace5> font_face5;
    if (FAILED(font_face->QueryInterface(IID_PPV_ARGS(&font_face5)))) {
      return;
    }
    if (!font_face5->HasVariations()) {
      return;
    }

    UINT32 font_axis_count = font_face5->GetFontAxisValueCount();
    ComPtr<IDWriteFontResource> font_resource;
    if (FAILED(font_face5->GetFontResource(&font_resource))) {
      return;
    }

    Vector<DWRITE_FONT_AXIS_VALUE, 8> font_axis_value(font_axis_count);
    if (FAILED(font_face5->GetFontAxisValues(font_axis_value.data(), font_axis_count))) {
      return;
    }
    for (UINT32 axis_index = 0; axis_index < font_axis_count; ++axis_index) {
      if (font_axis_value[axis_index].axisTag == DWRITE_FONT_AXIS_TAG_WEIGHT) {
        weight = static_cast<int>(font_axis_value[axis_index].value);
      }
      if (font_axis_value[axis_index].axisTag == DWRITE_FONT_AXIS_TAG_WIDTH) {
        float wdth_value = font_axis_value[axis_index].value;
        width = FontStyleWidthForWidthAxisValue(wdth_value);
      }
      if (font_axis_value[axis_index].axisTag == DWRITE_FONT_AXIS_TAG_SLANT &&
          slant != FontStyle::kItalic_Slant) {
        if (font_axis_value[axis_index].value == 0) {
          slant = FontStyle::kUpright_Slant;
        } else {
          slant = FontStyle::kOblique_Slant;
        }
      }
    }
  }();
#endif
  return FontStyle(weight, width, slant);
}

} // namespace bkfont
