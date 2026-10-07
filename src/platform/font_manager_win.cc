// Ported from: skia/src/ports/SkFontMgr_win_dw.cpp

#include "font_manager_win.h"

#include <cstring>
#include <cwchar>
#include <utility>

#include "base/mutex.h"
#include "base/vector.h"
#include "dwrite_font_file_stream.h"
#include "dwrite_internal.h"
#include "scaler_context.h"
#include "typeface_cache.h"
#include "typeface_freetype.h"
#include "typeface_proxy.h"

namespace bkit {

namespace {

// DWriteStyle from skia/src/utils/win/SkDWrite.h.
struct DWriteStyle {
  explicit DWriteStyle(const FontStyle& pattern) {
    weight = static_cast<DWRITE_FONT_WEIGHT>(pattern.GetWeight());
    width = static_cast<DWRITE_FONT_STRETCH>(pattern.GetWidth());
    switch (pattern.GetSlant()) {
    case FontStyle::kUpright_Slant:
      slant = DWRITE_FONT_STYLE_NORMAL;
      break;
    case FontStyle::kItalic_Slant:
      slant = DWRITE_FONT_STYLE_ITALIC;
      break;
    case FontStyle::kOblique_Slant:
      slant = DWRITE_FONT_STYLE_OBLIQUE;
      break;
    }
  }
  DWRITE_FONT_WEIGHT weight;
  DWRITE_FONT_STRETCH width;
  DWRITE_FONT_STYLE slant = DWRITE_FONT_STYLE_NORMAL;
};

// Korean fonts Gulim, Dotum, Batang, Gungsuh have bitmap strikes that get
// artifically emboldened by Windows without antialiasing. Korean users prefer
// these over the synthetic boldening performed by Skia. So let's make an
// exception for fonts with bitmap strikes and allow passing through Windows
// simulations for those, until Skia provides more control over simulations in
// font matching, see https://crbug.com/1258378
bool HasBitmapStrikes(IDWriteFont* font) {
  ComPtr<IDWriteFontFace> font_face;
  if (FAILED(font->CreateFontFace(&font_face))) {
    return false;
  }

  // AutoDWriteTable.
  const void* data = nullptr;
  UINT32 size = 0;
  void* lock = nullptr;
  BOOL exists = FALSE;
  if (FAILED(font_face->TryGetFontTable(DWRITE_MAKE_OPENTYPE_TAG('E', 'B', 'D', 'T'), &data, &size, &lock, &exists))) {
    return false;
  }
  if (exists) {
    font_face->ReleaseFontTable(lock);
  }
  return exists != FALSE;
}

// Iterate calls to GetFirstMatchingFont incrementally removing bold or italic
// styling that can trigger the simulations. Implementing it this way gets us a
// IDWriteFont that can be used as before and has the correct information on
// its own style. Stripping simulations from IDWriteFontFace is possible via
// IDWriteFontList1, IDWriteFontFaceReference and CreateFontFace, but this way
// we won't have a matching IDWriteFont which is still used in get_style().
//
// Chromium's skia/BUILD.gn defines SK_WIN_FONTMGR_NO_SIMULATIONS.
HRESULT FirstMatchingFontWithoutSimulations(IDWriteFontFamily* family,
                                            DWriteStyle dw_style,
                                            ComPtr<IDWriteFont>& font) {
  bool no_simulations = false;
  while (!no_simulations) {
    ComPtr<IDWriteFont> search_font;
    HRESULT hr = family->GetFirstMatchingFont(dw_style.weight, dw_style.width, dw_style.slant, &search_font);
    if (FAILED(hr)) {
      return hr;
    }
    DWRITE_FONT_SIMULATIONS simulations = search_font->GetSimulations();
    // If we still get simulations even though we're not asking for bold or
    // italic, we can't help it and exit the loop.

    no_simulations = simulations == DWRITE_FONT_SIMULATIONS_NONE ||
                     (dw_style.weight == DWRITE_FONT_WEIGHT_REGULAR &&
                      dw_style.slant == DWRITE_FONT_STYLE_NORMAL) ||
                     HasBitmapStrikes(search_font.Get());
    if (no_simulations) {
      font = std::move(search_font);
      break;
    }
    if (simulations & DWRITE_FONT_SIMULATIONS_BOLD) {
      dw_style.weight = DWRITE_FONT_WEIGHT_REGULAR;
      continue;
    }
    if (simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE) {
      dw_style.slant = DWRITE_FONT_STYLE_NORMAL;
      continue;
    }
  }
  return S_OK;
}

// DWriteFontTypeface::onOpenStream.
std::unique_ptr<StreamAsset> OpenDWriteFontFaceStream(IDWriteFontFace* font_face, int* ttc_index) {
  *ttc_index = static_cast<int>(font_face->GetIndex());

  UINT32 num_files = 0;
  if (FAILED(font_face->GetFiles(&num_files, nullptr))) {
    return nullptr;
  }
  if (num_files != 1) {
    return nullptr;
  }

  ComPtr<IDWriteFontFile> font_file;
  if (FAILED(font_face->GetFiles(&num_files, font_file.GetAddressOf()))) {
    return nullptr;
  }

  const void* font_file_key;
  UINT32 font_file_key_size;
  if (FAILED(font_file->GetReferenceKey(&font_file_key, &font_file_key_size))) {
    return nullptr;
  }

  ComPtr<IDWriteFontFileLoader> font_file_loader;
  if (FAILED(font_file->GetLoader(&font_file_loader))) {
    return nullptr;
  }

  ComPtr<IDWriteFontFileStream> font_file_stream;
  if (FAILED(font_file_loader->CreateStreamFromKey(font_file_key, font_file_key_size, &font_file_stream))) {
    return nullptr;
  }

  return std::make_unique<DWriteFontFileStream>(font_file_stream.Get());
}

// DWriteFontTypeface::onGetVariationDesignPosition: only VARIABLE axes, with
// their tags in OpenType byte order.
Vector<FontArguments::VariationPosition::Coordinate> DWriteVariationDesignPosition(IDWriteFontFace* font_face) {
  Vector<FontArguments::VariationPosition::Coordinate> result;
#if defined(NTDDI_WIN10_RS3) && NTDDI_VERSION >= NTDDI_WIN10_RS3
  ComPtr<IDWriteFontFace5> font_face5;
  if (FAILED(font_face->QueryInterface(IID_PPV_ARGS(&font_face5))) || !font_face5->HasVariations()) {
    return result;
  }
  UINT32 font_axis_count = font_face5->GetFontAxisValueCount();
  ComPtr<IDWriteFontResource> font_resource;
  if (FAILED(font_face5->GetFontResource(&font_resource))) {
    return result;
  }
  Vector<DWRITE_FONT_AXIS_VALUE, 8> font_axis_value(font_axis_count);
  if (FAILED(font_face5->GetFontAxisValues(font_axis_value.data(), font_axis_count))) {
    return result;
  }
  for (UINT32 axis_index = 0; axis_index < font_axis_count; ++axis_index) {
    if (font_resource->GetFontAxisAttributes(axis_index) & DWRITE_FONT_AXIS_ATTRIBUTES_VARIABLE) {
      // SkEndian_SwapBE32.
      const std::uint32_t tag = font_axis_value[axis_index].axisTag;
      result.push_back(FontArguments::VariationPosition::Coordinate{
          ((tag & 0xffu) << 24) | ((tag & 0xff00u) << 8) | ((tag & 0xff0000u) >> 8) | (tag >> 24),
          font_axis_value[axis_index].value});
    }
  }
#endif
  return result;
}

// Stands in for DWriteFontTypeface. As SkTypeface_fontconfig does for a
// fontconfig match, it wraps the FreeType typeface made from the matched
// font's file and keeps the matcher's family name and style. A DirectWrite
// bold simulation, which only survives SK_WIN_FONTMGR_NO_SIMULATIONS for fonts
// with bitmap strikes, becomes kEmbolden_Flag as FC_EMBOLDEN does. An oblique
// simulation is not applied.
class TypefaceDWrite final : public TypefaceProxy {
public:
  static std::shared_ptr<Typeface> Make(IDWriteFontFace* font_face,
                                        IDWriteFont* font,
                                        IDWriteFontFamily* font_family) {
    int ttc_index = 0;
    std::unique_ptr<StreamAsset> stream = OpenDWriteFontFaceStream(font_face, &ttc_index);
    if (!stream) {
      return nullptr;
    }
    const Vector<FontArguments::VariationPosition::Coordinate> position = DWriteVariationDesignPosition(font_face);
    FontArguments args;
    args.SetCollectionIndex(ttc_index);
    args.SetVariationDesignPosition({position.data(), static_cast<int>(position.size())});
    std::shared_ptr<Typeface> real_typeface = TypefaceFreeType::MakeFromStream(std::move(stream), args);
    if (!real_typeface) {
      return nullptr;
    }
    return std::make_shared<TypefaceDWrite>(std::move(real_typeface), font_face, font, font_family);
  }

  TypefaceDWrite(std::shared_ptr<Typeface> real_typeface,
                 IDWriteFontFace* font_face,
                 IDWriteFont* font,
                 IDWriteFontFamily* font_family)
      : TypefaceProxy(std::move(real_typeface), DWriteFontStyle(font, font_face)),
        dwrite_font_face_(font_face),
        dwrite_font_(font),
        dwrite_font_family_(font_family) {
  }

  ComPtr<IDWriteFontFace> dwrite_font_face_;
  ComPtr<IDWriteFont> dwrite_font_;
  ComPtr<IDWriteFontFamily> dwrite_font_family_;

protected:
  void OnGetFamilyName(String* family_name) const override {
    // DWriteFontTypeface::onGetFamilyName.
    ComPtr<IDWriteLocalizedStrings> family_names;
    if (FAILED(dwrite_font_family_->GetFamilyNames(&family_names))) {
      *family_name = String("");
      return;
    }
    String name = DWriteLocalizedString(family_names.Get(), nullptr);
    *family_name = name.IsNull() ? String("") : name;
  }

  FontStyle OnGetFontStyle() const override {
    return Typeface::OnGetFontStyle();
  }

  void OnFilterRec(ScalerContextRec* rec) const override {
    if (dwrite_font_->GetSimulations() & DWRITE_FONT_SIMULATIONS_BOLD) {
      rec->flags |= ScalerContext::kEmbolden_Flag;
    }

    TypefaceProxy::OnFilterRec(rec);
  }
};

bool AreSame(IUnknown* a, IUnknown* b, bool& same) {
  ComPtr<IUnknown> iunk_a;
  if (FAILED(a->QueryInterface(IID_PPV_ARGS(&iunk_a)))) {
    return false;
  }

  ComPtr<IUnknown> iunk_b;
  if (FAILED(b->QueryInterface(IID_PPV_ARGS(&iunk_b)))) {
    return false;
  }

  same = (iunk_a.Get() == iunk_b.Get());
  return true;
}

struct ProtoDWriteTypeface {
  IDWriteFontFace* dwrite_font_face;
  IDWriteFont* dwrite_font;
  IDWriteFontFamily* dwrite_font_family;
};

bool FindByDWriteFont(Typeface* cached, void* ctx) {
  TypefaceDWrite* csh_face = static_cast<TypefaceDWrite*>(cached);
  ProtoDWriteTypeface* ctx_face = static_cast<ProtoDWriteTypeface*>(ctx);

  // IDWriteFontFace5 introduced both Equals and HasVariations
  ComPtr<IDWriteFontFace5> csh_font_face5;
  ComPtr<IDWriteFontFace5> ctx_font_face5;
  csh_face->dwrite_font_face_->QueryInterface(IID_PPV_ARGS(&csh_font_face5));
  ctx_face->dwrite_font_face->QueryInterface(IID_PPV_ARGS(&ctx_font_face5));
  if (csh_font_face5 && ctx_font_face5) {
    return csh_font_face5->Equals(ctx_font_face5.Get()) != FALSE;
  }

  bool same;

  // Check to see if the two fonts are identical.
  if (!AreSame(csh_face->dwrite_font_.Get(), ctx_face->dwrite_font, same)) {
    return false;
  }
  if (same) {
    return true;
  }

  if (!AreSame(csh_face->dwrite_font_face_.Get(), ctx_face->dwrite_font_face, same)) {
    return false;
  }
  if (same) {
    return true;
  }

  // Check if the two fonts share the same loader and have the same key.
  UINT32 csh_num_files;
  UINT32 ctx_num_files;
  if (FAILED(csh_face->dwrite_font_face_->GetFiles(&csh_num_files, nullptr)) ||
      FAILED(ctx_face->dwrite_font_face->GetFiles(&ctx_num_files, nullptr))) {
    return false;
  }
  if (csh_num_files != ctx_num_files) {
    return false;
  }
  // Upstream asks for every file into a single pointer. Only single-file
  // faces are compared here so the call cannot write past it.
  if (csh_num_files != 1) {
    return false;
  }

  ComPtr<IDWriteFontFile> csh_font_file;
  ComPtr<IDWriteFontFile> ctx_font_file;
  if (FAILED(csh_face->dwrite_font_face_->GetFiles(&csh_num_files, csh_font_file.GetAddressOf())) ||
      FAILED(ctx_face->dwrite_font_face->GetFiles(&ctx_num_files, ctx_font_file.GetAddressOf()))) {
    return false;
  }

  // for (each file) { //we currently only admit fonts from one file.
  ComPtr<IDWriteFontFileLoader> csh_font_file_loader;
  ComPtr<IDWriteFontFileLoader> ctx_font_file_loader;
  if (FAILED(csh_font_file->GetLoader(&csh_font_file_loader)) ||
      FAILED(ctx_font_file->GetLoader(&ctx_font_file_loader))) {
    return false;
  }
  if (!AreSame(csh_font_file_loader.Get(), ctx_font_file_loader.Get(), same)) {
    return false;
  }
  if (!same) {
    return false;
  }
  //}

  const void* csh_ref_key;
  UINT32 csh_ref_key_size;
  const void* ctx_ref_key;
  UINT32 ctx_ref_key_size;
  if (FAILED(csh_font_file->GetReferenceKey(&csh_ref_key, &csh_ref_key_size)) ||
      FAILED(ctx_font_file->GetReferenceKey(&ctx_ref_key, &ctx_ref_key_size))) {
    return false;
  }
  if (csh_ref_key_size != ctx_ref_key_size) {
    return false;
  }
  if (0 != std::memcmp(csh_ref_key, ctx_ref_key, ctx_ref_key_size)) {
    return false;
  }

  // TODO: better means than comparing name strings?
  // NOTE: .ttc and fake bold/italic will end up here.
  ComPtr<IDWriteLocalizedStrings> csh_family_names;
  ComPtr<IDWriteLocalizedStrings> csh_face_names;
  if (FAILED(csh_face->dwrite_font_family_->GetFamilyNames(&csh_family_names)) ||
      FAILED(csh_face->dwrite_font_->GetFaceNames(&csh_face_names))) {
    return false;
  }
  UINT32 csh_family_name_length;
  UINT32 csh_face_name_length;
  if (FAILED(csh_family_names->GetStringLength(0, &csh_family_name_length)) ||
      FAILED(csh_face_names->GetStringLength(0, &csh_face_name_length))) {
    return false;
  }

  ComPtr<IDWriteLocalizedStrings> ctx_family_names;
  ComPtr<IDWriteLocalizedStrings> ctx_face_names;
  if (FAILED(ctx_face->dwrite_font_family->GetFamilyNames(&ctx_family_names)) ||
      FAILED(ctx_face->dwrite_font->GetFaceNames(&ctx_face_names))) {
    return false;
  }
  UINT32 ctx_family_name_length;
  UINT32 ctx_face_name_length;
  if (FAILED(ctx_family_names->GetStringLength(0, &ctx_family_name_length)) ||
      FAILED(ctx_face_names->GetStringLength(0, &ctx_face_name_length))) {
    return false;
  }

  if (csh_family_name_length != ctx_family_name_length ||
      csh_face_name_length != ctx_face_name_length) {
    return false;
  }

  std::unique_ptr<wchar_t[]> csh_family_name = std::make_unique<wchar_t[]>(csh_family_name_length + 1);
  std::unique_ptr<wchar_t[]> csh_face_name = std::make_unique<wchar_t[]>(csh_face_name_length + 1);
  if (FAILED(csh_family_names->GetString(0, csh_family_name.get(), csh_family_name_length + 1)) ||
      FAILED(csh_face_names->GetString(0, csh_face_name.get(), csh_face_name_length + 1))) {
    return false;
  }

  std::unique_ptr<wchar_t[]> ctx_family_name = std::make_unique<wchar_t[]>(ctx_family_name_length + 1);
  std::unique_ptr<wchar_t[]> ctx_face_name = std::make_unique<wchar_t[]>(ctx_face_name_length + 1);
  if (FAILED(ctx_family_names->GetString(0, ctx_family_name.get(), ctx_family_name_length + 1)) ||
      FAILED(ctx_face_names->GetString(0, ctx_face_name.get(), ctx_face_name_length + 1))) {
    return false;
  }

  return std::wcscmp(csh_family_name.get(), ctx_family_name.get()) == 0 &&
         std::wcscmp(csh_face_name.get(), ctx_face_name.get()) == 0;
}

// SkUTF::ToUTF16.
UINT32 ToUTF16(std::int32_t uni, WCHAR utf16[2]) {
  if (static_cast<std::uint32_t>(uni) > 0x10FFFF) {
    return 0;
  }
  int extra = (uni > 0xFFFF);
  if (extra) {
    utf16[0] = static_cast<WCHAR>((0xD800 - 64) + (uni >> 10));
    utf16[1] = static_cast<WCHAR>(0xDC00 | (uni & 0x3FF));
  } else {
    utf16[0] = static_cast<WCHAR>(uni);
  }
  return static_cast<UINT32>(1 + extra);
}

template <typename Interface>
class DWriteComObject : public Interface {
public:
  ULONG STDMETHODCALLTYPE AddRef() override {
    return InterlockedIncrement(&ref_count_);
  }
  ULONG STDMETHODCALLTYPE Release() override {
    ULONG new_count = InterlockedDecrement(&ref_count_);
    if (0 == new_count) {
      delete this;
    }
    return new_count;
  }

protected:
  virtual ~DWriteComObject() = default;

private:
  ULONG ref_count_ = 1;
};

class FontManagerDirectWrite;

class FontStyleSetDirectWrite final : public FontStyleSet {
public:
  FontStyleSetDirectWrite(std::shared_ptr<const FontManagerDirectWrite> font_mgr,
                          IDWriteFontFamily* font_family)
      : font_mgr_(std::move(font_mgr)),
        font_family_(font_family) {
  }

  int Count() override;
  void GetStyle(int index, FontStyle* fs, String* style_name) override;
  std::shared_ptr<Typeface> CreateTypeface(int index) override;
  std::shared_ptr<Typeface> MatchStyle(const FontStyle& pattern) override;

private:
  std::shared_ptr<const FontManagerDirectWrite> font_mgr_;
  ComPtr<IDWriteFontFamily> font_family_;
};

// SkFontMgr_DirectWrite.
class FontManagerDirectWrite final : public FontManager, public std::enable_shared_from_this<FontManagerDirectWrite> {
public:
  // locale_name_length and default_family_name_length must include the null
  // terminator.
  FontManagerDirectWrite(IDWriteFactory* factory, IDWriteFontCollection* font_collection,
                         IDWriteFontFallback* fallback,
                         const WCHAR* locale_name, int locale_name_length,
                         const WCHAR* default_family_name, int default_family_name_length)
      : factory_(factory),
        font_fallback_(fallback),
        font_collection_(font_collection),
        locale_name_(std::make_unique<WCHAR[]>(locale_name_length)),
        default_family_name_(std::make_unique<WCHAR[]>(default_family_name_length)) {
    std::memcpy(locale_name_.get(), locale_name, locale_name_length * sizeof(WCHAR));
    std::memcpy(default_family_name_.get(), default_family_name, default_family_name_length * sizeof(WCHAR));
  }

  // Creates a typeface using a typeface cache.
  std::shared_ptr<Typeface> MakeTypefaceFromDWriteFont(IDWriteFontFace* font_face,
                                                       IDWriteFont* font,
                                                       IDWriteFontFamily* font_family) const;

  IDWriteFontCollection* FontCollection() const {
    return font_collection_.Get();
  }
  const WCHAR* LocaleName() const {
    return locale_name_.get();
  }

protected:
  int OnCountFamilies() const override;
  void OnGetFamilyName(int index, String* family_name) const override;
  Vector<Typeface::LocalizedString> OnGetFamilyNames(int index) const override;
  std::shared_ptr<FontStyleSet> OnCreateStyleSet(int index) const override;
  std::shared_ptr<FontStyleSet> OnMatchFamily(const String& family_name) const override;
  std::shared_ptr<Typeface> OnMatchFamilyStyle(const String& family_name,
                                               const FontStyle& fontstyle) const override;
  std::shared_ptr<Typeface> OnMatchFamilyStyleCharacter(const String& family_name, const FontStyle&,
                                                        std::span<const String> bcp47, std::int32_t character) const override;
  std::shared_ptr<Typeface> OnMakeFromStreamIndex(std::unique_ptr<StreamAsset>, int ttc_index) const override;
  std::shared_ptr<Typeface> OnMakeFromStreamArgs(std::unique_ptr<StreamAsset>, const FontArguments&) const override;
  std::shared_ptr<Typeface> OnMakeFromData(std::shared_ptr<Data>, int ttc_index) const override;
  std::shared_ptr<Typeface> OnMakeFromFile(const String& path, int ttc_index) const override;
  std::shared_ptr<Typeface> OnLegacyMakeTypeface(const String& family_name, FontStyle) const override;

private:
  HRESULT GetByFamilyName(const WCHAR family_name[], ComPtr<IDWriteFontFamily>& font_family) const;
  std::shared_ptr<Typeface> Fallback(const WCHAR* dw_family_name, DWriteStyle,
                                     const WCHAR* dw_bcp47, UINT32 character) const;
  std::shared_ptr<Typeface> LayoutFallback(const WCHAR* dw_family_name, DWriteStyle,
                                           const WCHAR* dw_bcp47, UINT32 character) const;

  ComPtr<IDWriteFactory> factory_;
  ComPtr<IDWriteFontFallback> font_fallback_;
  ComPtr<IDWriteFontCollection> font_collection_;
  std::unique_ptr<WCHAR[]> locale_name_;
  std::unique_ptr<WCHAR[]> default_family_name_;
  mutable Mutex tf_cache_mutex_;
  mutable TypefaceCache tf_cache_;
};

std::shared_ptr<Typeface> FontManagerDirectWrite::MakeTypefaceFromDWriteFont(
    IDWriteFontFace* font_face,
    IDWriteFont* font,
    IDWriteFontFamily* font_family) const {
  AutoMutexExclusive ama(tf_cache_mutex_);
  ProtoDWriteTypeface spec = {font_face, font, font_family};
  std::shared_ptr<Typeface> face = tf_cache_.FindByProcAndRef(FindByDWriteFont, &spec);
  if (nullptr == face) {
    face = TypefaceDWrite::Make(font_face, font, font_family);
    if (face) {
      tf_cache_.Add(face);
    }
  }
  return face;
}

int FontManagerDirectWrite::OnCountFamilies() const {
  return static_cast<int>(font_collection_->GetFontFamilyCount());
}

void FontManagerDirectWrite::OnGetFamilyName(int index, String* family_name) const {
  ComPtr<IDWriteFontFamily> font_family;
  if (FAILED(font_collection_->GetFontFamily(static_cast<UINT32>(index), &font_family))) {
    return;
  }

  ComPtr<IDWriteLocalizedStrings> family_names;
  if (FAILED(font_family->GetFamilyNames(&family_names))) {
    return;
  }

  String name = DWriteLocalizedString(family_names.Get(), locale_name_.get());
  if (!name.IsNull()) {
    *family_name = name;
  }
}

Vector<Typeface::LocalizedString> FontManagerDirectWrite::OnGetFamilyNames(int index) const {
  Vector<Typeface::LocalizedString> result;
  ComPtr<IDWriteFontFamily> family;
  ComPtr<IDWriteLocalizedStrings> names;
  if (FAILED(font_collection_->GetFontFamily(static_cast<UINT32>(index), &family)) ||
      FAILED(family->GetFamilyNames(&names))) return result;
  result.ReserveInitialCapacity(names->GetCount());
  for (UINT32 i = 0; i < names->GetCount(); ++i) {
    UINT32 name_length = 0, locale_length = 0;
    if (FAILED(names->GetStringLength(i, &name_length)) ||
        FAILED(names->GetLocaleNameLength(i, &locale_length))) continue;
    auto name = std::make_unique<WCHAR[]>(static_cast<std::size_t>(name_length) + 1);
    auto locale = std::make_unique<WCHAR[]>(static_cast<std::size_t>(locale_length) + 1);
    if (FAILED(names->GetString(i, name.get(), name_length + 1)) ||
        FAILED(names->GetLocaleName(i, locale.get(), locale_length + 1))) continue;
    result.push_back(Typeface::LocalizedString{FromWide(name.get(), name_length), FromWide(locale.get(), locale_length)});
  }
  return result;
}

std::shared_ptr<FontStyleSet> FontManagerDirectWrite::OnCreateStyleSet(int index) const {
  ComPtr<IDWriteFontFamily> font_family;
  if (FAILED(font_collection_->GetFontFamily(static_cast<UINT32>(index), &font_family))) {
    return nullptr;
  }

  return std::make_shared<FontStyleSetDirectWrite>(shared_from_this(), font_family.Get());
}

std::shared_ptr<FontStyleSet> FontManagerDirectWrite::OnMatchFamily(const String& family_name) const {
  if (family_name.IsNull()) {
    return nullptr;
  }

  std::unique_ptr<wchar_t[]> dw_family_name = ToWide(family_name);

  UINT32 index;
  BOOL exists;
  if (FAILED(font_collection_->FindFamilyName(dw_family_name.get(), &index, &exists))) {
    return nullptr;
  }
  if (!exists) {
    return nullptr;
  }

  return OnCreateStyleSet(static_cast<int>(index));
}

std::shared_ptr<Typeface> FontManagerDirectWrite::OnMatchFamilyStyle(const String& family_name,
                                                                     const FontStyle& fontstyle) const {
  std::shared_ptr<FontStyleSet> sset(MatchFamily(family_name));
  return sset->MatchStyle(fontstyle);
}

class FontFallbackRenderer final : public DWriteComObject<IDWriteTextRenderer> {
public:
  FontFallbackRenderer(const FontManagerDirectWrite* outer, UINT32 character)
      : outer_(outer),
        character_(character),
        resolved_typeface_(nullptr) {
  }

  // IUnknown methods
  HRESULT STDMETHODCALLTYPE QueryInterface(IID const& riid, void** ppv_object) override {
    if (__uuidof(IUnknown) == riid ||
        __uuidof(IDWritePixelSnapping) == riid ||
        __uuidof(IDWriteTextRenderer) == riid) {
      *ppv_object = this;
      AddRef();
      return S_OK;
    }
    *ppv_object = nullptr;
    return E_FAIL;
  }

  // IDWriteTextRenderer methods
  HRESULT STDMETHODCALLTYPE DrawGlyphRun(
      void*,
      FLOAT,
      FLOAT,
      DWRITE_MEASURING_MODE,
      DWRITE_GLYPH_RUN const* glyph_run,
      DWRITE_GLYPH_RUN_DESCRIPTION const*,
      IUnknown*) override {
    if (!glyph_run->fontFace) {
      return E_INVALIDARG;
    }

    ComPtr<IDWriteFont> font;
    HRESULT hr = outer_->FontCollection()->GetFontFromFontFace(glyph_run->fontFace, &font);
    if (FAILED(hr)) {
      return hr;
    }

    // It is possible that the font passed does not actually have the
    // requested character, due to no font being found and getting the
    // fallback font. Check that the font actually contains the requested
    // character.
    BOOL exists;
    hr = font->HasCharacter(character_, &exists);
    if (FAILED(hr)) {
      return hr;
    }

    if (exists) {
      ComPtr<IDWriteFontFamily> font_family;
      hr = font->GetFontFamily(&font_family);
      if (FAILED(hr)) {
        return hr;
      }
      resolved_typeface_ = outer_->MakeTypefaceFromDWriteFont(glyph_run->fontFace,
                                                              font.Get(),
                                                              font_family.Get());
      has_simulations_ = (font->GetSimulations() != DWRITE_FONT_SIMULATIONS_NONE) &&
                         !HasBitmapStrikes(font.Get());
    }

    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT, FLOAT, DWRITE_UNDERLINE const*, IUnknown*) override {
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT, FLOAT, DWRITE_STRIKETHROUGH const*, IUnknown*) override {
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) override {
    return E_NOTIMPL;
  }

  // IDWritePixelSnapping methods
  HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* is_disabled) override {
    *is_disabled = FALSE;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* transform) override {
    const DWRITE_MATRIX ident = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    *transform = ident;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* pixels_per_dip) override {
    *pixels_per_dip = 1.0f;
    return S_OK;
  }

  std::shared_ptr<Typeface> ConsumeFallbackTypeface() {
    return std::move(resolved_typeface_);
  }

  bool FallbackTypefaceHasSimulations() {
    return has_simulations_;
  }

private:
  // The outer manager outlives every layout it draws.
  const FontManagerDirectWrite* outer_;
  UINT32 character_;
  std::shared_ptr<Typeface> resolved_typeface_;
  bool has_simulations_{false};
};

class FontFallbackSource final : public DWriteComObject<IDWriteTextAnalysisSource> {
public:
  FontFallbackSource(const WCHAR* string, UINT32 length, const WCHAR* locale,
                     IDWriteNumberSubstitution* number_substitution)
      : string_(string),
        length_(length),
        locale_(locale),
        number_substitution_(number_substitution) {
  }

  // IUnknown methods
  HRESULT STDMETHODCALLTYPE QueryInterface(IID const& riid, void** ppv_object) override {
    if (__uuidof(IUnknown) == riid ||
        __uuidof(IDWriteTextAnalysisSource) == riid) {
      *ppv_object = this;
      AddRef();
      return S_OK;
    }
    *ppv_object = nullptr;
    return E_FAIL;
  }

  // IDWriteTextAnalysisSource methods
  HRESULT STDMETHODCALLTYPE GetTextAtPosition(
      UINT32 text_position,
      WCHAR const** text_string,
      UINT32* text_length) override {
    if (length_ <= text_position) {
      *text_string = nullptr;
      *text_length = 0;
      return S_OK;
    }
    *text_string = string_ + text_position;
    *text_length = length_ - text_position;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetTextBeforePosition(
      UINT32 text_position,
      WCHAR const** text_string,
      UINT32* text_length) override {
    if (text_position < 1 || length_ <= text_position) {
      *text_string = nullptr;
      *text_length = 0;
      return S_OK;
    }
    *text_string = string_;
    *text_length = text_position;
    return S_OK;
  }

  DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetParagraphReadingDirection() override {
    // TODO: this is also interesting.
    return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
  }

  HRESULT STDMETHODCALLTYPE GetLocaleName(
      UINT32,
      UINT32*,
      WCHAR const** locale_name) override {
    *locale_name = locale_;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetNumberSubstitution(
      UINT32,
      UINT32*,
      IDWriteNumberSubstitution** number_substitution) override {
    *number_substitution = number_substitution_;
    return S_OK;
  }

private:
  const WCHAR* string_;
  UINT32 length_;
  const WCHAR* locale_;
  IDWriteNumberSubstitution* number_substitution_;
};

std::shared_ptr<Typeface> FontManagerDirectWrite::OnMatchFamilyStyleCharacter(
    const String& family_name, const FontStyle& style,
    std::span<const String> bcp47,
    std::int32_t character) const {
  DWriteStyle dw_style(style);

  const WCHAR* dw_family_name = nullptr;
  std::unique_ptr<wchar_t[]> dw_family_name_local;
  if (!family_name.IsNull()) {
    dw_family_name_local = ToWide(family_name);
    dw_family_name = dw_family_name_local.get();
  }

  const WCHAR* dw_bcp47;
  std::unique_ptr<wchar_t[]> dw_bcp47_local;
  if (bcp47.size() < 1) {
    dw_bcp47 = locale_name_.get();
  } else {
    // TODO: support fallback stack.
    // TODO: DirectWrite supports 'zh-CN' or 'zh-Hans', but 'zh' misses
    // completely and may produce a Japanese font.
    dw_bcp47_local = ToWide(bcp47[bcp47.size() - 1]);
    dw_bcp47 = dw_bcp47_local.get();
  }

  if (font_fallback_) {
    return Fallback(dw_family_name, dw_style, dw_bcp47, static_cast<UINT32>(character));
  }

  // LayoutFallback may use the system font collection for fallback.
  return LayoutFallback(dw_family_name, dw_style, dw_bcp47, static_cast<UINT32>(character));
}

std::shared_ptr<Typeface> FontManagerDirectWrite::Fallback(const WCHAR* dw_family_name,
                                                           DWriteStyle dw_style,
                                                           const WCHAR* dw_bcp47,
                                                           UINT32 character) const {
  WCHAR str[16];
  UINT32 str_len = ToUTF16(static_cast<std::int32_t>(character), str);

  if (!font_fallback_) {
    return nullptr;
  }

  ComPtr<IDWriteNumberSubstitution> number_substitution;
  if (FAILED(factory_->CreateNumberSubstitution(DWRITE_NUMBER_SUBSTITUTION_METHOD_NONE, dw_bcp47,
                                                TRUE, &number_substitution))) {
    return nullptr;
  }
  ComPtr<FontFallbackSource> font_fallback_source;
  font_fallback_source.Attach(new FontFallbackSource(str, str_len, dw_bcp47, number_substitution.Get()));

  UINT32 mapped_length;
  ComPtr<IDWriteFont> font;
  FLOAT scale;

  bool no_simulations = false;
  while (!no_simulations) {
    font.Reset();
    if (FAILED(font_fallback_->MapCharacters(font_fallback_source.Get(),
                                             0, // textPosition,
                                             str_len,
                                             font_collection_.Get(),
                                             dw_family_name,
                                             dw_style.weight,
                                             dw_style.slant,
                                             dw_style.width,
                                             &mapped_length,
                                             &font,
                                             &scale))) {
      return nullptr;
    }
    if (!font.Get()) {
      return nullptr;
    }

    DWRITE_FONT_SIMULATIONS simulations = font->GetSimulations();

    no_simulations = simulations == DWRITE_FONT_SIMULATIONS_NONE || HasBitmapStrikes(font.Get());

    if (simulations & DWRITE_FONT_SIMULATIONS_BOLD) {
      dw_style.weight = DWRITE_FONT_WEIGHT_REGULAR;
      continue;
    }

    if (simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE) {
      dw_style.slant = DWRITE_FONT_STYLE_NORMAL;
      continue;
    }
  }

  ComPtr<IDWriteFontFace> font_face;
  if (FAILED(font->CreateFontFace(&font_face))) {
    return nullptr;
  }

  ComPtr<IDWriteFontFamily> font_family;
  if (FAILED(font->GetFontFamily(&font_family))) {
    return nullptr;
  }
  return MakeTypefaceFromDWriteFont(font_face.Get(), font.Get(), font_family.Get());
}

std::shared_ptr<Typeface> FontManagerDirectWrite::LayoutFallback(const WCHAR* dw_family_name,
                                                                 DWriteStyle dw_style,
                                                                 const WCHAR* dw_bcp47,
                                                                 UINT32 character) const {
  WCHAR str[16];
  UINT32 str_len = ToUTF16(static_cast<std::int32_t>(character), str);

  bool no_simulations = false;
  std::shared_ptr<Typeface> return_typeface(nullptr);
  while (!no_simulations) {
    ComPtr<IDWriteTextFormat> fallback_format;
    if (FAILED(factory_->CreateTextFormat(dw_family_name ? dw_family_name : L"",
                                          font_collection_.Get(),
                                          dw_style.weight,
                                          dw_style.slant,
                                          dw_style.width,
                                          72.0f,
                                          dw_bcp47,
                                          &fallback_format))) {
      return nullptr;
    }

    // No matter how the font collection is set on this IDWriteTextLayout, it
    // is not possible to disable use of the system font collection in
    // fallback.
    ComPtr<IDWriteTextLayout> fallback_layout;
    if (FAILED(factory_->CreateTextLayout(str, str_len, fallback_format.Get(), 200.0f, 200.0f, &fallback_layout))) {
      return nullptr;
    }

    ComPtr<FontFallbackRenderer> font_fallback_renderer;
    font_fallback_renderer.Attach(new FontFallbackRenderer(this, character));

    if (FAILED(fallback_layout->SetFontCollection(font_collection_.Get(), {0, str_len}))) {
      return nullptr;
    }
    if (FAILED(fallback_layout->Draw(nullptr, font_fallback_renderer.Get(), 50.0f, 50.0f))) {
      return nullptr;
    }

    no_simulations = !font_fallback_renderer->FallbackTypefaceHasSimulations();

    if (no_simulations) {
      return_typeface = font_fallback_renderer->ConsumeFallbackTypeface();
    }

    if (dw_style.weight != DWRITE_FONT_WEIGHT_REGULAR) {
      dw_style.weight = DWRITE_FONT_WEIGHT_REGULAR;
      continue;
    }

    if (dw_style.slant != DWRITE_FONT_STYLE_NORMAL) {
      dw_style.slant = DWRITE_FONT_STYLE_NORMAL;
      continue;
    }
  }

  return return_typeface;
}

std::shared_ptr<Typeface> FontManagerDirectWrite::OnMakeFromStreamIndex(std::unique_ptr<StreamAsset> stream,
                                                                        int ttc_index) const {
  FontArguments args;
  args.SetCollectionIndex(ttc_index);
  return OnMakeFromStreamArgs(std::move(stream), args);
}

// Upstream makes a DWriteFontTypeface here. FreeType rasterizes every font in
// this port, so the stream becomes a FreeType typeface as SkFontMgr_Custom's
// onMakeFromStreamArgs does.
std::shared_ptr<Typeface> FontManagerDirectWrite::OnMakeFromStreamArgs(std::unique_ptr<StreamAsset> stream,
                                                                       const FontArguments& args) const {
  return TypefaceFreeType::MakeFromStream(std::move(stream), args);
}

std::shared_ptr<Typeface> FontManagerDirectWrite::OnMakeFromData(std::shared_ptr<Data> data, int ttc_index) const {
  return MakeFromStream(std::make_unique<MemoryStream>(std::move(data)), ttc_index);
}

std::shared_ptr<Typeface> FontManagerDirectWrite::OnMakeFromFile(const String& path, int ttc_index) const {
  return MakeFromStream(Stream::MakeFromFile(path), ttc_index);
}

HRESULT FontManagerDirectWrite::GetByFamilyName(const WCHAR wide_family_name[],
                                                ComPtr<IDWriteFontFamily>& font_family) const {
  UINT32 index;
  BOOL exists;
  HRESULT hr = font_collection_->FindFamilyName(wide_family_name, &index, &exists);
  if (FAILED(hr)) {
    return hr;
  }

  if (exists) {
    hr = font_collection_->GetFontFamily(index, &font_family);
    if (FAILED(hr)) {
      return hr;
    }
  }
  return S_OK;
}

std::shared_ptr<Typeface> FontManagerDirectWrite::OnLegacyMakeTypeface(const String& family_name,
                                                                       FontStyle style) const {
  ComPtr<IDWriteFontFamily> font_family;
  DWriteStyle dw_style(style);
  if (!family_name.IsNull()) {
    std::unique_ptr<wchar_t[]> dw_family_name = ToWide(family_name);
    GetByFamilyName(dw_family_name.get(), font_family);
    if (!font_family && font_fallback_) {
      return Fallback(dw_family_name.get(), dw_style, locale_name_.get(), 32);
    }
  }

  if (!font_family) {
    if (font_fallback_) {
      return Fallback(nullptr, dw_style, locale_name_.get(), 32);
    }
    // SPI_GETNONCLIENTMETRICS lfMessageFont can fail in Win8.
    // (DisallowWin32kSystemCalls) layoutFallback causes DCHECK in Chromium.
    // (Uses system font collection.)
    if (FAILED(GetByFamilyName(default_family_name_.get(), font_family))) {
      return nullptr;
    }
  }

  if (!font_family) {
    // Could not obtain the default font.
    if (FAILED(font_collection_->GetFontFamily(0, &font_family))) {
      return nullptr;
    }
  }

  ComPtr<IDWriteFont> font;
  if (FAILED(FirstMatchingFontWithoutSimulations(font_family.Get(), dw_style, font))) {
    return nullptr;
  }

  ComPtr<IDWriteFontFace> font_face;
  if (FAILED(font->CreateFontFace(&font_face))) {
    return nullptr;
  }

  return MakeTypefaceFromDWriteFont(font_face.Get(), font.Get(), font_family.Get());
}

int FontStyleSetDirectWrite::Count() {
  return static_cast<int>(font_family_->GetFontCount());
}

std::shared_ptr<Typeface> FontStyleSetDirectWrite::CreateTypeface(int index) {
  ComPtr<IDWriteFont> font;
  if (FAILED(font_family_->GetFont(static_cast<UINT32>(index), &font))) {
    return nullptr;
  }

  ComPtr<IDWriteFontFace> font_face;
  if (FAILED(font->CreateFontFace(&font_face))) {
    return nullptr;
  }

  return font_mgr_->MakeTypefaceFromDWriteFont(font_face.Get(), font.Get(), font_family_.Get());
}

void FontStyleSetDirectWrite::GetStyle(int index, FontStyle* fs, String* style_name) {
  ComPtr<IDWriteFont> font;
  if (FAILED(font_family_->GetFont(static_cast<UINT32>(index), &font))) {
    return;
  }

  if (fs) {
    ComPtr<IDWriteFontFace> face;
    if (FAILED(font->CreateFontFace(&face))) {
      return;
    }
    *fs = DWriteFontStyle(font.Get(), face.Get());
  }

  if (style_name) {
    ComPtr<IDWriteLocalizedStrings> face_names;
    if (SUCCEEDED(font->GetFaceNames(&face_names))) {
      String name = DWriteLocalizedString(face_names.Get(), font_mgr_->LocaleName());
      if (!name.IsNull()) {
        *style_name = name;
      }
    }
  }
}

std::shared_ptr<Typeface> FontStyleSetDirectWrite::MatchStyle(const FontStyle& pattern) {
  ComPtr<IDWriteFont> font;
  DWriteStyle dw_style(pattern);

  if (FAILED(FirstMatchingFontWithoutSimulations(font_family_.Get(), dw_style, font))) {
    return nullptr;
  }

  ComPtr<IDWriteFontFace> font_face;
  if (FAILED(font->CreateFontFace(&font_face))) {
    return nullptr;
  }

  return font_mgr_->MakeTypefaceFromDWriteFont(font_face.Get(), font.Get(), font_family_.Get());
}

} // namespace

// SkFontMgr_New_DirectWrite(nullptr, nullptr, nullptr).
std::shared_ptr<FontManager> MakeFontManagerDirectWrite() {
  // sk_get_dwrite_factory.
  ComPtr<IDWriteFactory> factory;
  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(factory.GetAddressOf())))) {
    return nullptr;
  }

  ComPtr<IDWriteFontCollection> system_font_collection;
  if (FAILED(factory->GetSystemFontCollection(&system_font_collection, FALSE))) {
    return nullptr;
  }

  // It is possible to have been provided a font fallback when factory2 is not
  // available.
  ComPtr<IDWriteFontFallback> system_font_fallback;
  ComPtr<IDWriteFactory2> factory2;
  if (SUCCEEDED(factory.As(&factory2))) {
    if (FAILED(factory2->GetSystemFontFallback(&system_font_fallback))) {
      return nullptr;
    }
  }

  const WCHAR* default_family_name = L"";
  int default_family_name_len = 1;
  NONCLIENTMETRICSW metrics;
  metrics.cbSize = sizeof(metrics);

  if (nullptr == system_font_fallback) {
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
      default_family_name = metrics.lfMessageFont.lfFaceName;
      default_family_name_len = LF_FACESIZE;
    }
  }

  WCHAR locale_name_storage[LOCALE_NAME_MAX_LENGTH];
  const WCHAR* locale_name = L"";
  int locale_name_len = 1;

  int size = GetUserDefaultLocaleName(locale_name_storage, LOCALE_NAME_MAX_LENGTH);
  if (size) {
    locale_name = locale_name_storage;
    locale_name_len = size;
  }

  return std::make_shared<FontManagerDirectWrite>(factory.Get(), system_font_collection.Get(), system_font_fallback.Get(),
                                                  locale_name, locale_name_len,
                                                  default_family_name, default_family_name_len);
}

} // namespace bkit
