/*
 * Copyright 2014 Google Inc.
 * BSD license retained in dwrite_internal.h.
 * Port of third_party/skia/src/ports/SkTypeface_win_dw.cpp and
 * SkScalerContext_win_dw.cpp. See corresponding function names below.
 */
#include "dwrite_internal.h"
#include "font_host_freetype.h"

#include <cmath>

namespace bkfont {
namespace {

std::atomic<std::int32_t> next_font_id{1};

class DWriteTable final {
public:
  DWriteTable(IDWriteFontFace* face, std::uint32_t tag)
      : face_(face) {
    if (FAILED(face_->TryGetFontTable(SwapFontTag(tag), &data_, &size_, &context_, &exists_))) {
      exists_ = FALSE;
    }
  }
  ~DWriteTable() {
    if (exists_)
      face_->ReleaseFontTable(context_);
  }
  DWriteTable(const DWriteTable&) = delete;
  DWriteTable& operator=(const DWriteTable&) = delete;
  const std::uint8_t* Data() const {
    return static_cast<const std::uint8_t*>(data_);
  }
  std::size_t Size() const {
    return exists_ ? size_ : 0;
  }
  bool Exists() const {
    return exists_ != FALSE;
  }

private:
  ComPtr<IDWriteFontFace> face_;
  const void* data_ = nullptr;
  UINT32 size_ = 0;
  void* context_ = nullptr;
  BOOL exists_ = FALSE;
};

std::int16_t ReadSigned16(const std::uint8_t* bytes) {
  return static_cast<std::int16_t>(
      (static_cast<std::uint16_t>(bytes[0]) << 8) | bytes[1]);
}

// SkFontDescriptor::SkFontStyleWidthForWidthAxisValue uses piecewise-linear
// interpolation followed by SkScalarRoundToInt, not nearest percentage class.
int WidthForAxisValue(float value) {
  constexpr float widths[] = {50, 62.5f, 75, 87.5f, 100, 112.5f, 125, 150, 200};
  if (value <= widths[0])
    return 1;
  for (int i = 1; i < 9; ++i) {
    if (value <= widths[i]) {
      const float interpolated = static_cast<float>(i) + (value - widths[i - 1]) / (widths[i] - widths[i - 1]);
      return static_cast<int>(std::floor(interpolated + 0.5f));
    }
  }
  return 9;
}

// SkDWriteFontFileStream (src/utils/win/SkDWriteFontFileStream.cpp): the
// getLength and getMemoryBase subset.
class DWriteFontFileStream final : public FontFileStream {
public:
  explicit DWriteFontFileStream(IDWriteFontFileStream* font_file_stream)
      : font_file_stream_(font_file_stream) {
  }
  ~DWriteFontFileStream() override {
    if (fragment_lock_) {
      font_file_stream_->ReleaseFileFragment(fragment_lock_);
    }
  }

  std::size_t GetLength() const override {
    UINT64 real_file_size = 0;
    font_file_stream_->GetFileSize(&real_file_size);
    if (real_file_size > std::numeric_limits<std::size_t>::max()) {
      return 0;
    }
    return static_cast<std::size_t>(real_file_size);
  }

  const void* GetMemoryBase() override {
    if (locked_memory_) {
      return locked_memory_;
    }

    UINT64 file_size;
    if (FAILED(font_file_stream_->GetFileSize(&file_size)))
      return nullptr;
    if (FAILED(font_file_stream_->ReadFileFragment(&locked_memory_, 0, file_size, &fragment_lock_)))
      return nullptr;
    return locked_memory_;
  }

private:
  ComPtr<IDWriteFontFileStream> font_file_stream_;
  const void* locked_memory_ = nullptr;
  void* fragment_lock_ = nullptr;
};

} // namespace

// src/utils/win/SkDWrite.cpp: sk_get_locale_string.
String DWriteLocalizedString(IDWriteLocalizedStrings* strings,
                             const wchar_t* locale) {
  if (!strings)
    return String();
  UINT32 index = 0;
  if (locale) {
    BOOL exists = FALSE;
    strings->FindLocaleName(locale, &index, &exists);
    if (!exists)
      index = 0;
  }
  UINT32 length = 0;
  if (FAILED(strings->GetStringLength(index, &length)))
    return String();
  std::unique_ptr<wchar_t[]> name =
      std::make_unique<wchar_t[]>(static_cast<std::size_t>(length) + 1);
  if (FAILED(strings->GetString(index, name.get(), length + 1)))
    return String();
  return FromWide(name.get(), length);
}

// DWriteFontTypeface::GetStyle. Preserve variable wght/wdth/slnt overrides.
FontStyle DWriteStyle(IDWriteFont* font, IDWriteFontFace* face) {
  FontStyle style;
  style.weight = static_cast<int>(font->GetWeight());
  style.stretch = static_cast<int>(font->GetStretch());
  switch (font->GetStyle()) {
  case DWRITE_FONT_STYLE_ITALIC:
    style.slant = FontSlant::kItalic;
    break;
  case DWRITE_FONT_STYLE_OBLIQUE:
    style.slant = FontSlant::kOblique;
    break;
  default:
    style.slant = FontSlant::kNormal;
    break;
  }
  ComPtr<IDWriteFontFace5> face5;
  if (FAILED(face->QueryInterface(IID_PPV_ARGS(&face5))) || !face5->HasVariations())
    return style;
  ComPtr<IDWriteFontResource> resource;
  if (FAILED(face5->GetFontResource(&resource)))
    return style;
  const UINT32 count = face5->GetFontAxisValueCount();
  std::unique_ptr<DWRITE_FONT_AXIS_VALUE[]> axes =
      std::make_unique<DWRITE_FONT_AXIS_VALUE[]>(count);
  if (FAILED(face5->GetFontAxisValues(axes.get(), count)))
    return style;
  for (const DWRITE_FONT_AXIS_VALUE& axis :
       std::span<const DWRITE_FONT_AXIS_VALUE>(axes.get(), count)) {
    if (axis.axisTag == DWRITE_FONT_AXIS_TAG_WEIGHT)
      style.weight = static_cast<int>(axis.value);
    if (axis.axisTag == DWRITE_FONT_AXIS_TAG_WIDTH)
      style.stretch = WidthForAxisValue(axis.value);
    if (axis.axisTag == DWRITE_FONT_AXIS_TAG_SLANT && style.slant != FontSlant::kItalic)
      style.slant = axis.value == 0 ? FontSlant::kNormal : FontSlant::kOblique;
  }
  return style;
}

FontFace::FontFace(std::unique_ptr<Impl> implementation)
    : impl_(std::move(implementation)) {
  impl_->unique_id = static_cast<std::uint32_t>(
      next_font_id.fetch_add(1, std::memory_order_relaxed));
  impl_->InitializePalette();
}
// SkTypeface_FreeType::~SkTypeface_FreeType.
FontFace::~FontFace() {
  if (face_rec_) {
    AutoMutexExclusive ac(FreeTypeMutex());
    face_rec_.reset();
  }
}

// DWriteFontTypeface::onOpenStream.
std::unique_ptr<FontFileStream> FontFace::OpenStream(int* ttc_index) const {
  *ttc_index = static_cast<int>(impl_->face->GetIndex());

  UINT32 num_files = 0;
  if (FAILED(impl_->face->GetFiles(&num_files, nullptr)))
    return nullptr;
  if (num_files != 1)
    return nullptr;

  ComPtr<IDWriteFontFile> font_file;
  if (FAILED(impl_->face->GetFiles(&num_files, font_file.GetAddressOf())))
    return nullptr;

  const void* font_file_key;
  UINT32 font_file_key_size;
  if (FAILED(font_file->GetReferenceKey(&font_file_key, &font_file_key_size)))
    return nullptr;

  ComPtr<IDWriteFontFileLoader> font_file_loader;
  if (FAILED(font_file->GetLoader(&font_file_loader)))
    return nullptr;

  ComPtr<IDWriteFontFileStream> font_file_stream;
  if (FAILED(font_file_loader->CreateStreamFromKey(font_file_key, font_file_key_size, &font_file_stream)))
    return nullptr;

  return std::make_unique<DWriteFontFileStream>(font_file_stream.Get());
}

String FontFace::FamilyName() const {
  ComPtr<IDWriteLocalizedStrings> names;
  if (FAILED(impl_->family->GetFamilyNames(&names)))
    return String();
  return DWriteLocalizedString(names.Get());
}

String FontFace::PostScriptName() const {
  ComPtr<IDWriteLocalizedStrings> names;
  BOOL exists = FALSE;
  if (FAILED(impl_->font->GetInformationalStrings(
          DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME,
          &names,
          &exists))
      || !exists)
    return String();
  return DWriteLocalizedString(names.Get());
}

Vector<LocalizedFontName> FontFace::FamilyNames() const {
  // onCreateFamilyNameIterator: prefer the name table (family/preferred/WWS)
  // and consult DirectWrite only when obtaining that table failed.
  const Vector<std::uint8_t> table = TableData(0x6e616d65u);
  if (!table.empty())
    return FamilyNamesFromNameTable(
        std::span<const std::uint8_t>(table.data(), table.size()));
  Vector<LocalizedFontName> result;
  ComPtr<IDWriteLocalizedStrings> names;
  if (FAILED(impl_->family->GetFamilyNames(&names)))
    return result;
  for (UINT32 index = 0; index < names->GetCount(); ++index) {
    UINT32 name_length = 0;
    UINT32 locale_length = 0;
    if (FAILED(names->GetStringLength(index, &name_length)) || FAILED(names->GetLocaleNameLength(index, &locale_length)))
      break;
    std::unique_ptr<wchar_t[]> name =
        std::make_unique<wchar_t[]>(static_cast<std::size_t>(name_length) + 1);
    std::unique_ptr<wchar_t[]> locale =
        std::make_unique<wchar_t[]>(static_cast<std::size_t>(locale_length) + 1);
    if (FAILED(names->GetString(index, name.get(), name_length + 1)) || FAILED(names->GetLocaleName(index, locale.get(), locale_length + 1)))
      break;
    result.push_back(LocalizedFontName{FromWide(name.get(), name_length),
                                       FromWide(locale.get(), locale_length)});
  }
  return result;
}

FontStyle FontFace::Style() const {
  return DWriteStyle(impl_->font.Get(), impl_->face.Get());
}
std::uint32_t FontFace::UniqueId() const {
  return impl_->unique_id;
}
std::uint32_t FontFace::CollectionIndex() const {
  return impl_->face->GetIndex();
}
std::uint16_t FontFace::GlyphCount() const {
  return impl_->face->GetGlyphCount();
}
std::uint16_t FontFace::UnitsPerEm() const {
  DWRITE_FONT_METRICS metrics{};
  impl_->face->GetMetrics(&metrics);
  return metrics.designUnitsPerEm;
}
bool FontFace::ContainsCharacter(std::uint32_t codepoint) const {
  return GlyphForCharacter(codepoint) != 0;
}
std::uint16_t FontFace::GlyphForCharacter(std::uint32_t codepoint) const {
  UINT16 glyph = 0;
  if (FAILED(impl_->face->GetGlyphIndices(&codepoint, 1, &glyph)))
    return 0;
  return glyph;
}
bool FontFace::CharactersToGlyphs(std::span<const std::uint32_t> codepoints,
                                  std::span<std::uint16_t> glyphs) const {
  if (codepoints.size() != glyphs.size() || codepoints.size() > std::numeric_limits<UINT32>::max())
    return false;
  return SUCCEEDED(impl_->face->GetGlyphIndices(
      codepoints.data(),
      static_cast<UINT32>(codepoints.size()),
      glyphs.data()));
}
bool FontFace::IsFixedPitch() const {
  ComPtr<IDWriteFontFace1> face1;
  return SUCCEEDED(impl_->face.As(&face1)) && face1->IsMonospacedFont();
}
bool FontFace::HasColorGlyphs() const {
  ComPtr<IDWriteFactory2> factory2;
  ComPtr<IDWriteFontFace2> face2;
  return SUCCEEDED(impl_->factory.As(&factory2)) && SUCCEEDED(impl_->face.As(&face2)) && face2->IsColorFont();
}
bool FontFace::HasVariations() const {
  ComPtr<IDWriteFontFace5> face5;
  return SUCCEEDED(impl_->face.As(&face5)) && face5->HasVariations();
}
bool FontFace::HasSimulations() const {
  return impl_->face->GetSimulations() != DWRITE_FONT_SIMULATIONS_NONE;
}

// DWriteFontTypeface::onGetTableData / onCopyTableData. The public table tag is
// big-endian as in OpenType and HarfBuzz; the native DWrite tag is little-endian.
std::size_t FontFace::ReadTable(std::uint32_t tag, std::size_t offset,
                                std::span<std::uint8_t> destination) const {
  DWriteMutexLock lock(impl_->face.Get());
  DWriteTable table(impl_->face.Get(), tag);
  if (offset > table.Size())
    return 0;
  const std::size_t count = std::min(destination.size(), table.Size() - offset);
  if (count)
    std::memcpy(destination.data(), table.Data() + offset, count);
  return count;
}
Vector<std::uint8_t> FontFace::TableData(std::uint32_t tag) const {
  DWriteMutexLock lock(impl_->face.Get());
  DWriteTable table(impl_->face.Get(), tag);
  Vector<std::uint8_t> bytes(table.Size());
  if (table.Size())
    std::memcpy(bytes.data(), table.Data(), table.Size());
  return bytes;
}
bool FontFace::HasTable(std::uint32_t tag) const {
  DWriteMutexLock lock(impl_->face.Get());
  return DWriteTable(impl_->face.Get(), tag).Exists();
}

// SkScalerContext_DW::generateFontMetrics. Font coordinates are retained for
// ascent/decorations; top/bottom remain y-down like SkFontMetrics. The caller
// chooses natural versus GDI-compatible measurement, rather than a SkScalerRec.
PlatformFontMetrics FontFace::GetMetrics(float size, FontMeasuringMode mode) const {
  DWriteMutexLock lock(impl_->face.Get());
  PlatformFontMetrics result;
  DWRITE_FONT_METRICS metrics{};
  if (mode == FontMeasuringMode::kNatural) {
    impl_->face->GetMetrics(&metrics);
  } else if (FAILED(impl_->face->GetGdiCompatibleMetrics(size, 1, nullptr, &metrics))) {
    return result;
  }
  if (!metrics.designUnitsPerEm)
    return result;
  const float scale = size / metrics.designUnitsPerEm;
  result.ascender = scale * metrics.ascent;
  result.descender = scale * metrics.descent;
  result.line_gap = scale * metrics.lineGap;
  result.cap_height = scale * metrics.capHeight;
  result.x_height = scale * metrics.xHeight;
  result.underline_position = scale * metrics.underlinePosition;
  result.underline_thickness = scale * metrics.underlineThickness;
  result.strikeout_position = scale * metrics.strikethroughPosition;
  result.strikeout_thickness = scale * metrics.strikethroughThickness;
  // Average character width is not generated by the upstream DirectWrite
  // scaler. It remains zero so SimpleFontData's glyph fallback still applies.
  result.bounds_valid = !HasVariations();
  ComPtr<IDWriteFontFace1> face1;
  if (SUCCEEDED(impl_->face.As(&face1))) {
    DWRITE_FONT_METRICS1 metrics1{};
    face1->GetMetrics(&metrics1);
    result.top = -scale * metrics1.glyphBoxTop;
    result.bottom = -scale * metrics1.glyphBoxBottom;
    result.x_min = scale * metrics1.glyphBoxLeft;
    result.x_max = scale * metrics1.glyphBoxRight;
    result.max_char_width = result.x_max - result.x_min;
    return result;
  }
  DWriteTable head(impl_->face.Get(), 0x68656164u);
  if (head.Size() >= 54 && head.Data()[0] == 0 && head.Data()[1] == 1 && head.Data()[2] == 0 && head.Data()[3] == 0) {
    result.x_min = scale * ReadSigned16(head.Data() + 36);
    result.top = -scale * ReadSigned16(head.Data() + 42);
    result.x_max = scale * ReadSigned16(head.Data() + 40);
    result.bottom = -scale * ReadSigned16(head.Data() + 38);
    result.max_char_width = result.x_max - result.x_min;
    return result;
  }
  result.bounds_valid = false;
  result.top = -result.ascender;
  result.bottom = result.descender;
  return result;
}

bool FontFace::GetDesignGlyphMetrics(
    std::uint16_t glyph, PlatformDesignGlyphMetrics* result) const {
  if (!result || glyph >= GlyphCount())
    return false;
  DWriteMutexLock lock(impl_->face.Get());
  DWRITE_GLYPH_METRICS metrics{};
  if (FAILED(impl_->face->GetDesignGlyphMetrics(&glyph, 1, &metrics, FALSE)))
    return false;
  *result = {metrics.leftSideBearing, metrics.advanceWidth, metrics.rightSideBearing, metrics.topSideBearing, metrics.advanceHeight, metrics.bottomSideBearing, metrics.verticalOriginY};
  return true;
}

// SkScalerContext_DW::setAdvance: preserve out-of-range rejection, natural/GDI
// branches and final GDI pixel rounding. The design box extension is an adapter
// facility; it does not claim equivalence to Skia's raster bounds.
PlatformGlyphMetrics FontFace::GetGlyphMetrics(
    std::uint16_t glyph, float size, bool vertical, FontMeasuringMode mode) const {
  PlatformGlyphMetrics result;
  if (glyph >= GlyphCount())
    return result;
  DWriteMutexLock lock(impl_->face.Get());
  DWRITE_GLYPH_METRICS metrics{};
  HRESULT status = S_OK;
  if (mode == FontMeasuringMode::kNatural) {
    status = impl_->face->GetDesignGlyphMetrics(&glyph, 1, &metrics, FALSE);
  } else {
    status = impl_->face->GetGdiCompatibleGlyphMetrics(
        size,
        1,
        nullptr,
        mode == FontMeasuringMode::kGdiNatural,
        &glyph,
        1,
        &metrics,
        FALSE);
  }
  const std::uint16_t units = UnitsPerEm();
  if (FAILED(status) || !units)
    return result;
  const float scale = size / units;
  result.advance_x = vertical ? 0 : scale * metrics.advanceWidth;
  result.advance_y = vertical ? scale * metrics.advanceHeight : 0;
  if (mode != FontMeasuringMode::kNatural) {
    result.advance_x = std::floor(result.advance_x + 0.5f);
    result.advance_y = std::floor(result.advance_y + 0.5f);
  }
  result.left = scale * metrics.leftSideBearing;
  result.top = scale * (metrics.topSideBearing - metrics.verticalOriginY);
  result.width = scale * (static_cast<float>(metrics.advanceWidth) - metrics.leftSideBearing - metrics.rightSideBearing);
  result.height = scale * (static_cast<float>(metrics.advanceHeight) - metrics.topSideBearing - metrics.bottomSideBearing);
  return result;
}

// DWriteFontTypeface::onGetVariationDesignPosition. Only VARIABLE axes escape.
Vector<PlatformFontVariationAxis> FontFace::VariationCoordinates() const {
  Vector<PlatformFontVariationAxis> result;
  ComPtr<IDWriteFontFace5> face5;
  if (FAILED(impl_->face.As(&face5)) || !face5->HasVariations())
    return result;
  ComPtr<IDWriteFontResource> resource;
  const UINT32 count = face5->GetFontAxisValueCount();
  std::unique_ptr<DWRITE_FONT_AXIS_VALUE[]> values =
      std::make_unique<DWRITE_FONT_AXIS_VALUE[]>(count);
  if (FAILED(face5->GetFontResource(&resource)) || FAILED(face5->GetFontAxisValues(values.get(), count)))
    return result;
  for (UINT32 i = 0; i < count; ++i) {
    if (resource->GetFontAxisAttributes(i) & DWRITE_FONT_AXIS_ATTRIBUTES_VARIABLE)
      result.push_back(PlatformFontVariationAxis{SwapFontTag(values[i].axisTag), values[i].value});
  }
  return result;
}

// DWriteFontTypeface::onGetVariationDesignParameters.
Vector<FontVariationParameter> FontFace::VariationParameters() const {
  Vector<FontVariationParameter> result;
  ComPtr<IDWriteFontFace5> face5;
  if (FAILED(impl_->face.As(&face5)) || !face5->HasVariations())
    return result;
  ComPtr<IDWriteFontResource> resource;
  const UINT32 count = face5->GetFontAxisValueCount();
  std::unique_ptr<DWRITE_FONT_AXIS_VALUE[]> defaults =
      std::make_unique<DWRITE_FONT_AXIS_VALUE[]>(count);
  std::unique_ptr<DWRITE_FONT_AXIS_RANGE[]> ranges =
      std::make_unique<DWRITE_FONT_AXIS_RANGE[]>(count);
  if (FAILED(face5->GetFontResource(&resource)) || FAILED(resource->GetFontAxisRanges(ranges.get(), count)) || FAILED(resource->GetDefaultFontAxisValues(defaults.get(), count)))
    return result;
  for (UINT32 i = 0; i < count; ++i) {
    const DWRITE_FONT_AXIS_ATTRIBUTES attributes = resource->GetFontAxisAttributes(i);
    if (attributes & DWRITE_FONT_AXIS_ATTRIBUTES_VARIABLE)
      result.push_back(FontVariationParameter{SwapFontTag(defaults[i].axisTag), ranges[i].minValue, defaults[i].value, ranges[i].maxValue, (attributes & DWRITE_FONT_AXIS_ATTRIBUTES_HIDDEN) != 0});
  }
  return result;
}

// DWriteFontTypeface::onMakeClone (palette and collection index are separate
// concerns in this adapter). Preserve current values and last matching override.
std::shared_ptr<FontFace> FontFace::WithVariations(
    std::span<const PlatformFontVariationAxis> coordinates) {
  ComPtr<IDWriteFontFace5> face5;
  if (FAILED(impl_->face.As(&face5)) || !face5->HasVariations())
    return shared_from_this();
  const UINT32 count = face5->GetFontAxisValueCount();
  std::unique_ptr<DWRITE_FONT_AXIS_VALUE[]> values =
      std::make_unique<DWRITE_FONT_AXIS_VALUE[]>(count);
  if (FAILED(face5->GetFontAxisValues(values.get(), count)))
    return nullptr;
  for (DWRITE_FONT_AXIS_VALUE& value :
       std::span<DWRITE_FONT_AXIS_VALUE>(values.get(), count)) {
    for (const PlatformFontVariationAxis& coordinate : coordinates) {
      if (SwapFontTag(value.axisTag) == coordinate.tag)
        value.value = coordinate.value;
    }
  }
  ComPtr<IDWriteFontResource> resource;
  ComPtr<IDWriteFontFace5> varied;
  if (FAILED(face5->GetFontResource(&resource)) || FAILED(resource->CreateFontFace(impl_->font->GetSimulations(), values.get(), count, &varied)))
    return nullptr;
  auto implementation = std::make_unique<Impl>();
  implementation->factory = impl_->factory;
  implementation->font = impl_->font;
  implementation->family = impl_->family;
  implementation->loaders = impl_->loaders;
  implementation->palette_index = impl_->palette_index;
  implementation->palette_override_count = impl_->palette_override_count;
  implementation->palette_overrides =
      std::make_unique<PlatformPaletteOverride[]>(impl_->palette_override_count);
  for (std::size_t i = 0; i < impl_->palette_override_count; ++i)
    implementation->palette_overrides[i] = impl_->palette_overrides[i];
  if (FAILED(varied.As(&implementation->face)))
    return nullptr;
  return std::shared_ptr<FontFace>(new FontFace(std::move(implementation)));
}

// DWriteFontTypeface::initializePalette: out-of-range base palette falls back
// to palette zero; overrides still apply in source order.
void FontFace::Impl::InitializePalette() {
  ComPtr<IDWriteFactory2> factory2;
  ComPtr<IDWriteFontFace2> face2;
  if (FAILED(factory.As(&factory2)) || FAILED(face.As(&face2)) || !face2->IsColorFont() || !face2->GetColorPaletteCount())
    return;
  const UINT32 base = palette_index < face2->GetColorPaletteCount() ? palette_index : 0;
  const UINT32 count = face2->GetPaletteEntryCount();
  std::unique_ptr<DWRITE_COLOR_F[]> native_colors =
      std::make_unique<DWRITE_COLOR_F[]>(count);
  if (FAILED(face2->GetPaletteEntries(base, 0, count, native_colors.get())))
    return;
  palette_colors = std::make_unique<std::uint32_t[]>(count);
  palette_color_count = count;
  const auto channel = [](float value) -> std::uint32_t {
    return static_cast<std::uint32_t>(std::floor(value * 255 + 0.5f));
  };
  for (UINT32 i = 0; i < count; ++i) {
    const DWRITE_COLOR_F& color = native_colors[i];
    palette_colors[i] = (channel(color.a) << 24) | (channel(color.r) << 16) | (channel(color.g) << 8) | channel(color.b);
  }
  for (const PlatformPaletteOverride& override :
       std::span<const PlatformPaletteOverride>(palette_overrides.get(),
                                                palette_override_count)) {
    if (override.index < count)
      palette_colors[override.index] = override.color;
  }
}

std::shared_ptr<FontFace> FontFace::WithPalette(
    std::uint16_t palette_index, std::span<const PlatformPaletteOverride> overrides) {
  bool same = palette_index == impl_->palette_index && overrides.size() == impl_->palette_override_count;
  if (same) {
    for (std::size_t i = 0; i < overrides.size(); ++i) {
      if (overrides[i].index != impl_->palette_overrides[i].index || overrides[i].color != impl_->palette_overrides[i].color) {
        same = false;
        break;
      }
    }
  }
  if (same)
    return shared_from_this();
  auto implementation = std::make_unique<Impl>();
  implementation->factory = impl_->factory;
  implementation->face = impl_->face;
  implementation->font = impl_->font;
  implementation->family = impl_->family;
  implementation->loaders = impl_->loaders;
  implementation->palette_index = palette_index;
  implementation->palette_override_count = overrides.size();
  implementation->palette_overrides =
      std::make_unique<PlatformPaletteOverride[]>(overrides.size());
  for (std::size_t i = 0; i < overrides.size(); ++i)
    implementation->palette_overrides[i] = overrides[i];
  return std::shared_ptr<FontFace>(new FontFace(std::move(implementation)));
}

Vector<std::uint32_t> FontFace::PaletteColors() const {
  Vector<std::uint32_t> colors(impl_->palette_color_count);
  for (std::size_t i = 0; i < impl_->palette_color_count; ++i)
    colors[i] = impl_->palette_colors[i];
  return colors;
}
std::uint16_t FontFace::RequestedPaletteIndex() const {
  return impl_->palette_index;
}

} // namespace bkfont
