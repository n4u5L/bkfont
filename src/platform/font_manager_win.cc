/*
 * Copyright 2012, 2014 Google Inc.
 * BSD license retained in dwrite_internal.h.
 * Port of third_party/skia/src/ports/SkFontMgr_win_dw.cpp,
 * SkTypeface_win_dw.cpp (custom collection loaders / MakeFromStream), and
 * src/utils/win/SkDWriteFontFileStream.cpp (memory stream bounds behavior).
 */
#include "dwrite_internal.h"

namespace blink {
namespace {

template <typename Interface>
class DWriteComObject : public Interface {
public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
    if (iid == __uuidof(IUnknown) || iid == __uuidof(Interface)) {
      *result = static_cast<Interface*>(this);
      AddRef();
      return S_OK;
    }
    *result = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override {
    return InterlockedIncrement(&references_);
  }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG count = InterlockedDecrement(&references_);
    if (!count)
      delete this;
    return count;
  }

protected:
  virtual ~DWriteComObject() = default;

private:
  ULONG references_ = 1;
};

struct NativeStyle {
  explicit NativeStyle(const FontStyle& style)
      : weight(static_cast<DWRITE_FONT_WEIGHT>(style.weight)),
        width(static_cast<DWRITE_FONT_STRETCH>(style.stretch)),
        slant(style.slant == FontSlant::kItalic ? DWRITE_FONT_STYLE_ITALIC : style.slant == FontSlant::kOblique ? DWRITE_FONT_STYLE_OBLIQUE
                                                                                                                : DWRITE_FONT_STYLE_NORMAL) {
  }
  DWRITE_FONT_WEIGHT weight;
  DWRITE_FONT_STRETCH width;
  DWRITE_FONT_STYLE slant;
};

bool HasBitmapStrikes(IDWriteFont* font) {
  ComPtr<IDWriteFontFace> face;
  if (FAILED(font->CreateFontFace(&face)))
    return false;
  const void* data = nullptr;
  UINT32 size = 0;
  void* context = nullptr;
  BOOL exists = FALSE;
  if (FAILED(face->TryGetFontTable(DWRITE_MAKE_OPENTYPE_TAG('E', 'B', 'D', 'T'),
                                   &data,
                                   &size,
                                   &context,
                                   &exists)))
    return false;
  if (exists)
    face->ReleaseFontTable(context);
  return exists != FALSE;
}

// FirstMatchingFontWithoutSimulations. Chromium's skia/BUILD.gn enables
// SK_WIN_FONTMGR_NO_SIMULATIONS; keep that branch including Korean bitmap fonts.
HRESULT FirstMatchingFontWithoutSimulations(IDWriteFontFamily* family,
                                            NativeStyle style,
                                            IDWriteFont** result) {
  bool no_simulations = false;
  while (!no_simulations) {
    ComPtr<IDWriteFont> font;
    const HRESULT status = family->GetFirstMatchingFont(
        style.weight,
        style.width,
        style.slant,
        &font);
    if (FAILED(status))
      return status;
    const DWRITE_FONT_SIMULATIONS simulations = font->GetSimulations();
    no_simulations = simulations == DWRITE_FONT_SIMULATIONS_NONE || (style.weight == DWRITE_FONT_WEIGHT_REGULAR && style.slant == DWRITE_FONT_STYLE_NORMAL) || HasBitmapStrikes(font.Get());
    if (no_simulations) {
      *result = font.Detach();
      break;
    }
    if (simulations & DWRITE_FONT_SIMULATIONS_BOLD) {
      style.weight = DWRITE_FONT_WEIGHT_REGULAR;
      continue;
    }
    if (simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE) {
      style.slant = DWRITE_FONT_STYLE_NORMAL;
      continue;
    }
  }
  return S_OK;
}

bool SameComObject(IUnknown* first, IUnknown* second) {
  ComPtr<IUnknown> first_identity;
  ComPtr<IUnknown> second_identity;
  return first && second && SUCCEEDED(first->QueryInterface(IID_PPV_ARGS(&first_identity))) && SUCCEEDED(second->QueryInterface(IID_PPV_ARGS(&second_identity))) && first_identity.Get() == second_identity.Get();
}

// FindByDWriteFont: Face5 Equals, COM identity, file loader/key, then names.
bool SameFont(IDWriteFontFace* first_face, IDWriteFont* first_font,
              IDWriteFontFamily* first_family, IDWriteFontFace* second_face,
              IDWriteFont* second_font, IDWriteFontFamily* second_family) {
  ComPtr<IDWriteFontFace5> first5;
  ComPtr<IDWriteFontFace5> second5;
  first_face->QueryInterface(IID_PPV_ARGS(&first5));
  second_face->QueryInterface(IID_PPV_ARGS(&second5));
  if (first5 && second5)
    return first5->Equals(second5.Get()) != FALSE;
  if (SameComObject(first_font, second_font) || SameComObject(first_face, second_face))
    return true;
  UINT32 first_count = 0;
  UINT32 second_count = 0;
  if (FAILED(first_face->GetFiles(&first_count, nullptr)) || FAILED(second_face->GetFiles(&second_count, nullptr)) || first_count != second_count || first_count != 1)
    return false;
  ComPtr<IDWriteFontFile> first_file;
  ComPtr<IDWriteFontFile> second_file;
  ComPtr<IDWriteFontFileLoader> first_loader;
  ComPtr<IDWriteFontFileLoader> second_loader;
  if (FAILED(first_face->GetFiles(&first_count, first_file.GetAddressOf())) || FAILED(second_face->GetFiles(&second_count, second_file.GetAddressOf())) || FAILED(first_file->GetLoader(&first_loader)) || FAILED(second_file->GetLoader(&second_loader)) || !SameComObject(first_loader.Get(), second_loader.Get()))
    return false;
  const void* first_key = nullptr;
  const void* second_key = nullptr;
  UINT32 first_length = 0;
  UINT32 second_length = 0;
  if (FAILED(first_file->GetReferenceKey(&first_key, &first_length)) || FAILED(second_file->GetReferenceKey(&second_key, &second_length)) || first_length != second_length || std::memcmp(first_key, second_key, first_length) != 0)
    return false;
  ComPtr<IDWriteLocalizedStrings> first_family_names;
  ComPtr<IDWriteLocalizedStrings> second_family_names;
  ComPtr<IDWriteLocalizedStrings> first_face_names;
  ComPtr<IDWriteLocalizedStrings> second_face_names;
  if (FAILED(first_family->GetFamilyNames(&first_family_names)) || FAILED(second_family->GetFamilyNames(&second_family_names)) || FAILED(first_font->GetFaceNames(&first_face_names)) || FAILED(second_font->GetFaceNames(&second_face_names)))
    return false;
  const String first_family_name = DWriteLocalizedString(first_family_names.Get());
  const String second_family_name = DWriteLocalizedString(second_family_names.Get());
  const String first_face_name = DWriteLocalizedString(first_face_names.Get());
  const String second_face_name = DWriteLocalizedString(second_face_names.Get());
  return !first_family_name.IsNull() && !second_family_name.IsNull() && !first_face_name.IsNull() && !second_face_name.IsNull() && first_family_name == second_family_name && first_face_name == second_face_name;
}

// FontFallbackSource. In particular, retain the upstream boundary behavior of
// GetTextBeforePosition and the unchanged output-length handling for locale and
// number substitution; this adapter does not silently repair source behavior.
class FontFallbackSource final : public DWriteComObject<IDWriteTextAnalysisSource> {
public:
  FontFallbackSource(const WCHAR* text, UINT32 length, const WCHAR* locale,
                     IDWriteNumberSubstitution* substitution)
      : text_(text),
        length_(length),
        locale_(locale),
        substitution_(substitution) {
  }
  HRESULT STDMETHODCALLTYPE GetTextAtPosition(
      UINT32 position, const WCHAR** text, UINT32* length) override {
    if (length_ <= position) {
      *text = nullptr;
      *length = 0;
      return S_OK;
    }
    *text = text_ + position;
    *length = length_ - position;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetTextBeforePosition(
      UINT32 position, const WCHAR** text, UINT32* length) override {
    if (position < 1 || length_ <= position) {
      *text = nullptr;
      *length = 0;
      return S_OK;
    }
    *text = text_;
    *length = position;
    return S_OK;
  }
  DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetParagraphReadingDirection() override {
    return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
  }
  HRESULT STDMETHODCALLTYPE GetLocaleName(
      UINT32, UINT32*, const WCHAR** locale) override {
    *locale = locale_;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetNumberSubstitution(
      UINT32, UINT32*, IDWriteNumberSubstitution** substitution) override {
    *substitution = substitution_.Get();
    return S_OK;
  }

private:
  const WCHAR* text_;
  UINT32 length_;
  const WCHAR* locale_;
  ComPtr<IDWriteNumberSubstitution> substitution_;
};

// FontFallbackRenderer from SkFontMgr_win_dw.cpp. This does not draw: it captures
// the face chosen by DirectWrite's pre-IDWriteFontFallback text-layout path.
class FontFallbackRenderer final : public DWriteComObject<IDWriteTextRenderer> {
public:
  FontFallbackRenderer(IDWriteFontCollection* collection, UINT32 codepoint)
      : collection_(collection),
        codepoint_(codepoint) {
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
    if (iid == __uuidof(IDWritePixelSnapping)) {
      *result = static_cast<IDWritePixelSnapping*>(this);
      AddRef();
      return S_OK;
    }
    return DWriteComObject::QueryInterface(iid, result);
  }
  HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*, FLOAT, FLOAT,
                                         DWRITE_MEASURING_MODE, const DWRITE_GLYPH_RUN* run,
                                         const DWRITE_GLYPH_RUN_DESCRIPTION*, IUnknown*) override {
    if (!run->fontFace)
      return E_INVALIDARG;
    ComPtr<IDWriteFont> candidate;
    HRESULT status = collection_->GetFontFromFontFace(run->fontFace, &candidate);
    if (FAILED(status))
      return status;
    BOOL exists = FALSE;
    status = candidate->HasCharacter(codepoint_, &exists);
    if (FAILED(status))
      return status;
    if (exists) {
      ComPtr<IDWriteFontFamily> candidate_family;
      status = candidate->GetFontFamily(&candidate_family);
      if (FAILED(status))
        return status;
      face = run->fontFace;
      font = std::move(candidate);
      family = std::move(candidate_family);
      has_simulations = font->GetSimulations() != DWRITE_FONT_SIMULATIONS_NONE && !HasBitmapStrikes(font.Get());
    }
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT, FLOAT,
                                          const DWRITE_UNDERLINE*, IUnknown*) override {
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT, FLOAT,
                                              const DWRITE_STRIKETHROUGH*, IUnknown*) override {
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT,
                                             IDWriteInlineObject*, BOOL, BOOL, IUnknown*) override {
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* disabled) override {
    *disabled = FALSE;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* transform) override {
    *transform = {1, 0, 0, 1, 0, 0};
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* value) override {
    *value = 1;
    return S_OK;
  }
  ComPtr<IDWriteFontFace> face;
  ComPtr<IDWriteFont> font;
  ComPtr<IDWriteFontFamily> family;
  bool has_simulations = false;

private:
  ComPtr<IDWriteFontCollection> collection_;
  UINT32 codepoint_;
};

UINT32 EncodeCharacter(UINT32 codepoint, WCHAR* text) {
  if (codepoint > 0x10ffffu || (codepoint >= 0xd800u && codepoint <= 0xdfffu))
    return 0;
  if (codepoint <= 0xffffu) {
    text[0] = static_cast<WCHAR>(codepoint);
    return 1;
  }
  codepoint -= 0x10000u;
  text[0] = static_cast<WCHAR>(0xd800u + (codepoint >> 10));
  text[1] = static_cast<WCHAR>(0xdc00u + (codepoint & 0x3ffu));
  return 2;
}

// The loader and its COM streams share the lifetime of one immutable font file.
// Its byte count is fixed when the file is copied into the owned array.
struct FontFileData {
  std::unique_ptr<std::uint8_t[]> bytes;
  std::size_t size = 0;
};

// SkDWriteFontFileStreamWrapper::ReadFileFragment's memory-backed branch.
class MemoryFontFileStream final : public DWriteComObject<IDWriteFontFileStream> {
public:
  explicit MemoryFontFileStream(std::shared_ptr<const FontFileData> data)
      : data_(std::move(data)) {
  }
  HRESULT STDMETHODCALLTYPE ReadFileFragment(const void** start, UINT64 offset,
                                             UINT64 size, void** context) override {
    *start = nullptr;
    *context = nullptr;
    const UINT64 length = data_->size;
    if (offset > length || size > length - offset || offset + size > std::numeric_limits<std::size_t>::max())
      return E_FAIL;
    *start = data_->bytes.get() + static_cast<std::size_t>(offset);
    return S_OK;
  }
  void STDMETHODCALLTYPE ReleaseFileFragment(void*) override {
  }
  HRESULT STDMETHODCALLTYPE GetFileSize(UINT64* size) override {
    *size = data_->size;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetLastWriteTime(UINT64* time) override {
    *time = 0;
    return E_NOTIMPL;
  }

private:
  std::shared_ptr<const FontFileData> data_;
};

class StreamFontFileLoader final : public DWriteComObject<IDWriteFontFileLoader> {
public:
  explicit StreamFontFileLoader(std::shared_ptr<const FontFileData> data)
      : data_(std::move(data)) {
  }
  HRESULT STDMETHODCALLTYPE CreateStreamFromKey(const void*, UINT32,
                                                IDWriteFontFileStream** result) override {
    *result = new MemoryFontFileStream(data_);
    return S_OK;
  }

private:
  std::shared_ptr<const FontFileData> data_;
};

class StreamFontFileEnumerator final : public DWriteComObject<IDWriteFontFileEnumerator> {
public:
  StreamFontFileEnumerator(IDWriteFactory* factory, IDWriteFontFileLoader* loader)
      : factory_(factory),
        loader_(loader) {
  }
  HRESULT STDMETHODCALLTYPE MoveNext(BOOL* current) override {
    *current = FALSE;
    if (!has_next_)
      return S_OK;
    has_next_ = false;
    const UINT32 key = 0;
    const HRESULT status = factory_->CreateCustomFontFileReference(
        &key,
        sizeof(key),
        loader_.Get(),
        &current_file_);
    if (FAILED(status))
      return status;
    *current = TRUE;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetCurrentFontFile(IDWriteFontFile** file) override {
    if (!current_file_) {
      *file = nullptr;
      return E_FAIL;
    }
    return current_file_.CopyTo(file);
  }

private:
  ComPtr<IDWriteFactory> factory_;
  ComPtr<IDWriteFontFileLoader> loader_;
  ComPtr<IDWriteFontFile> current_file_;
  bool has_next_ = true;
};

class StreamFontCollectionLoader final : public DWriteComObject<IDWriteFontCollectionLoader> {
public:
  explicit StreamFontCollectionLoader(IDWriteFontFileLoader* loader)
      : loader_(loader) {
  }
  HRESULT STDMETHODCALLTYPE CreateEnumeratorFromKey(IDWriteFactory* factory,
                                                    const void*, UINT32, IDWriteFontFileEnumerator** enumerator) override {
    *enumerator = new StreamFontFileEnumerator(factory, loader_.Get());
    return S_OK;
  }

private:
  ComPtr<IDWriteFontFileLoader> loader_;
};

} // namespace

// DWriteFontTypeface::Loaders and SkAutoIDWriteUnregister. Registration ownership
// transfers to FontFace; failure paths unregister without terminating the process.
class FontCollectionLoaders final {
public:
  explicit FontCollectionLoaders(IDWriteFactory* factory)
      : factory_(factory) {
  }
  ~FontCollectionLoaders() {
    if (collection_registered_)
      factory_->UnregisterFontCollectionLoader(collection_loader_.Get());
    if (file_registered_)
      factory_->UnregisterFontFileLoader(file_loader_.Get());
  }
  HRESULT Initialize(std::shared_ptr<const FontFileData> data) {
    file_loader_.Attach(new StreamFontFileLoader(std::move(data)));
    HRESULT status = factory_->RegisterFontFileLoader(file_loader_.Get());
    if (FAILED(status))
      return status;
    file_registered_ = true;
    collection_loader_.Attach(new StreamFontCollectionLoader(file_loader_.Get()));
    status = factory_->RegisterFontCollectionLoader(collection_loader_.Get());
    if (FAILED(status))
      return status;
    collection_registered_ = true;
    return S_OK;
  }
  IDWriteFontCollectionLoader* CollectionLoader() const {
    return collection_loader_.Get();
  }

private:
  ComPtr<IDWriteFactory> factory_;
  ComPtr<IDWriteFontFileLoader> file_loader_;
  ComPtr<IDWriteFontCollectionLoader> collection_loader_;
  bool file_registered_ = false;
  bool collection_registered_ = false;
};

struct FontManager::Impl {
  ComPtr<IDWriteFactory> factory;
  ComPtr<IDWriteFontCollection> collection;
  ComPtr<IDWriteFontFallback> fallback;
  String locale;
  String default_family;
  mutable std::mutex cache_mutex;
  // SkTypefaceCache owns faces strongly; external references prevent purging.
  // Keep its historical default (SkGraphics.cpp) and quarter-cache purge rule.
  mutable Vector<std::shared_ptr<FontFace>> faces;
  static constexpr int cache_count_limit = 1024;

  void Purge(int number) const {
    std::size_t index = 0;
    while (index < faces.size()) {
      if (faces[index].use_count() == 1) {
        faces[index] = std::move(faces.back());
        faces.pop_back();
        if (--number == 0)
          return;
      } else {
        ++index;
      }
    }
  }

  std::shared_ptr<FontFace> MakeFace(IDWriteFontFace* native_face,
                                     IDWriteFont* native_font, IDWriteFontFamily* family,
                                     std::shared_ptr<FontCollectionLoaders> loaders = nullptr) const {
    std::scoped_lock lock(cache_mutex);
    const bool cache_face = !loaders;
    if (cache_face) {
      for (const std::shared_ptr<FontFace>& cached : faces) {
        if (SameFont(cached->impl_->face.Get(), cached->impl_->font.Get(), cached->impl_->family.Get(), native_face, native_font, family))
          return cached;
      }
    }
    auto face_impl = std::make_unique<FontFace::Impl>();
    face_impl->factory = factory;
    face_impl->face = native_face;
    face_impl->font = native_font;
    face_impl->family = family;
    face_impl->loaders = std::move(loaders);
    auto face = std::shared_ptr<FontFace>(new FontFace(std::move(face_impl)));
    // MakeFromStream constructs a standalone typeface; the manager's cache is
    // only used by makeTypefaceFromDWriteFont for installed/fallback faces.
    if (cache_face) {
      if (faces.size() >= cache_count_limit)
        Purge(cache_count_limit >> 2);
      faces.push_back(face);
    }
    return face;
  }

  // SkFontMgr_DirectWrite::fallback, retaining the source's simulation loop.
  std::shared_ptr<FontFace> Fallback(const WCHAR* family, NativeStyle style,
                                     const WCHAR* locale_name, UINT32 codepoint) const {
    WCHAR text[16];
    const UINT32 length = EncodeCharacter(codepoint, text);
    if (!fallback)
      return nullptr;
    ComPtr<IDWriteNumberSubstitution> substitution;
    if (FAILED(factory->CreateNumberSubstitution(DWRITE_NUMBER_SUBSTITUTION_METHOD_NONE,
                                                 locale_name,
                                                 TRUE,
                                                 &substitution)))
      return nullptr;
    ComPtr<FontFallbackSource> source;
    source.Attach(new FontFallbackSource(text, length, locale_name, substitution.Get()));
    ComPtr<IDWriteFont> font;
    bool no_simulations = false;
    while (!no_simulations) {
      font.Reset();
      UINT32 mapped_length = 0;
      FLOAT scale = 0;
      if (FAILED(fallback->MapCharacters(source.Get(), 0, length, collection.Get(), family, style.weight, style.slant, style.width, &mapped_length, &font, &scale)) || !font)
        return nullptr;
      const DWRITE_FONT_SIMULATIONS simulations = font->GetSimulations();
      no_simulations = simulations == DWRITE_FONT_SIMULATIONS_NONE || HasBitmapStrikes(font.Get());
      if (simulations & DWRITE_FONT_SIMULATIONS_BOLD) {
        style.weight = DWRITE_FONT_WEIGHT_REGULAR;
        continue;
      }
      if (simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE) {
        style.slant = DWRITE_FONT_STYLE_NORMAL;
        continue;
      }
    }
    ComPtr<IDWriteFontFace> face;
    ComPtr<IDWriteFontFamily> matched_family;
    if (FAILED(font->CreateFontFace(&face)) || FAILED(font->GetFontFamily(&matched_family)))
      return nullptr;
    // MapCharacters' scale is intentionally unused, as it is upstream.
    return MakeFace(face.Get(), font.Get(), matched_family.Get());
  }

  // SkFontMgr_DirectWrite::layoutFallback; retained for factories predating v2.
  std::shared_ptr<FontFace> LayoutFallback(const WCHAR* family, NativeStyle style,
                                           const WCHAR* locale_name, UINT32 codepoint) const {
    WCHAR text[16];
    const UINT32 length = EncodeCharacter(codepoint, text);
    bool no_simulations = false;
    std::shared_ptr<FontFace> result;
    while (!no_simulations) {
      ComPtr<IDWriteTextFormat> format;
      ComPtr<IDWriteTextLayout> layout;
      if (FAILED(factory->CreateTextFormat(family ? family : L"", collection.Get(), style.weight, style.slant, style.width, 72, locale_name, &format)) || FAILED(factory->CreateTextLayout(text, length, format.Get(), 200, 200, &layout)))
        return nullptr;
      ComPtr<FontFallbackRenderer> renderer;
      renderer.Attach(new FontFallbackRenderer(collection.Get(), codepoint));
      if (FAILED(layout->SetFontCollection(collection.Get(), {0, length})) || FAILED(layout->Draw(nullptr, renderer.Get(), 50, 50)))
        return nullptr;
      no_simulations = !renderer->has_simulations;
      if (no_simulations && renderer->face)
        result = MakeFace(renderer->face.Get(), renderer->font.Get(), renderer->family.Get());
      if (style.weight != DWRITE_FONT_WEIGHT_REGULAR) {
        style.weight = DWRITE_FONT_WEIGHT_REGULAR;
        continue;
      }
      if (style.slant != DWRITE_FONT_STYLE_NORMAL) {
        style.slant = DWRITE_FONT_STYLE_NORMAL;
        continue;
      }
    }
    return result;
  }
};

FontManager::FontManager(std::unique_ptr<Impl> implementation)
    : impl_(std::move(implementation)) {
}
FontManager::~FontManager() = default;

void FontManager::PurgeUnusedFaces() const {
  std::scoped_lock lock(impl_->cache_mutex);
  impl_->Purge(static_cast<int>(impl_->faces.size()));
}

// SkFontMgr_New_DirectWrite, with a shared DirectWrite factory and the system
// collection. Browser-process services/sandbox proxies are outside this port.
std::shared_ptr<FontManager> FontManager::Create() {
  auto implementation = std::make_unique<Impl>();
  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(implementation->factory.GetAddressOf()))) || FAILED(implementation->factory->GetSystemFontCollection(&implementation->collection, FALSE)))
    return nullptr;
  ComPtr<IDWriteFactory2> factory2;
  if (SUCCEEDED(implementation->factory.As(&factory2)) && FAILED(factory2->GetSystemFontFallback(&implementation->fallback)))
    return nullptr;
  WCHAR locale[LOCALE_NAME_MAX_LENGTH];
  const int locale_length = GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
  implementation->locale = locale_length ? FromWide(locale, locale_length - 1) : String(u"");
  implementation->default_family = String(u"");
  if (!implementation->fallback) {
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
      implementation->default_family = FromWide(metrics.lfMessageFont.lfFaceName,
                                                std::wcslen(metrics.lfMessageFont.lfFaceName));
  }
  return std::shared_ptr<FontManager>(new FontManager(std::move(implementation)));
}

std::uint32_t FontManager::FamilyCount() const {
  return impl_->collection->GetFontFamilyCount();
}
String FontManager::FamilyName(std::uint32_t index) const {
  ComPtr<IDWriteFontFamily> family;
  ComPtr<IDWriteLocalizedStrings> names;
  if (FAILED(impl_->collection->GetFontFamily(index, &family)) || FAILED(family->GetFamilyNames(&names)))
    return String();
  const std::unique_ptr<wchar_t[]> locale = ToWide(impl_->locale);
  return DWriteLocalizedString(names.Get(), locale.get());
}
Vector<FontStyle> FontManager::FamilyStyles(const String& name) const {
  Vector<FontStyle> result;
  const std::unique_ptr<wchar_t[]> wide = ToWide(name);
  UINT32 index = 0;
  BOOL exists = FALSE;
  ComPtr<IDWriteFontFamily> family;
  if (FAILED(impl_->collection->FindFamilyName(wide.get(), &index, &exists)) || !exists || FAILED(impl_->collection->GetFontFamily(index, &family)))
    return result;
  for (UINT32 i = 0; i < family->GetFontCount(); ++i) {
    ComPtr<IDWriteFont> font;
    ComPtr<IDWriteFontFace> face;
    if (FAILED(family->GetFont(i, &font)) || FAILED(font->CreateFontFace(&face)))
      break;
    result.push_back(DWriteStyle(font.Get(), face.Get()));
  }
  return result;
}
std::shared_ptr<FontFace> FontManager::MatchFamily(
    const String& name, const FontStyle& style) const {
  if (name.IsNull())
    return nullptr;
  const std::unique_ptr<wchar_t[]> wide = ToWide(name);
  UINT32 index = 0;
  BOOL exists = FALSE;
  ComPtr<IDWriteFontFamily> family;
  ComPtr<IDWriteFont> font;
  ComPtr<IDWriteFontFace> face;
  if (FAILED(impl_->collection->FindFamilyName(wide.get(), &index, &exists)) || !exists || FAILED(impl_->collection->GetFontFamily(index, &family)) || FAILED(FirstMatchingFontWithoutSimulations(family.Get(), NativeStyle(style), &font)) || FAILED(font->CreateFontFace(&face)))
    return nullptr;
  return impl_->MakeFace(face.Get(), font.Get(), family.Get());
}
std::shared_ptr<FontFace> FontManager::MatchCharacter(const String& family,
                                                      const FontStyle& style, const String& locale, std::uint32_t codepoint) const {
  const std::unique_ptr<wchar_t[]> wide_family = ToWide(family);
  const std::unique_ptr<wchar_t[]> wide_locale =
      ToWide(locale.IsNull() ? impl_->locale : locale);
  const WCHAR* family_name = family.IsNull() ? nullptr : wide_family.get();
  // onMatchFamilyStyleCharacter accepts a locale stack but DWrite sees only its
  // last item. Blink already selects that locale before entering this adapter.
  if (impl_->fallback)
    return impl_->Fallback(family_name, NativeStyle(style), wide_locale.get(), codepoint);
  return impl_->LayoutFallback(family_name, NativeStyle(style), wide_locale.get(), codepoint);
}

// content/browser/renderer_host/dwrite_font_proxy_impl_win.cc:
// DWriteFontProxyImpl::MatchUniqueFont, plus FontFilePathAndTtcIndex from
// dwrite_font_file_util_win.cc. Keep PostScript-before-full-name matching and
// reopen the first match at its TTC index, as Blink's local() path does.
// Copyright 2018, 2019 The Chromium Authors. BSD license in dwrite_internal.h.
std::shared_ptr<FontFace> FontManager::MatchUniqueName(const String& unique_name) const {
  ComPtr<IDWriteFontCollection1> collection1;
  ComPtr<IDWriteFontSet> font_set;
  if (FAILED(impl_->collection.As(&collection1)) || FAILED(collection1->GetFontSet(&font_set)))
    return nullptr;
  const std::unique_ptr<wchar_t[]> name = ToWide(unique_name);
  ComPtr<IDWriteFontSet> filtered;
  const auto filter = [&font_set, &filtered, &name](DWRITE_FONT_PROPERTY_ID id) -> bool {
    DWRITE_FONT_PROPERTY property{id, name.get(), L""};
    filtered.Reset();
    return SUCCEEDED(font_set->GetMatchingFonts(&property, 1, &filtered));
  };
  if (!filter(DWRITE_FONT_PROPERTY_ID_POSTSCRIPT_NAME))
    return nullptr;
  if (!filtered->GetFontCount() && !filter(DWRITE_FONT_PROPERTY_ID_FULL_NAME))
    return nullptr;
  if (!filtered->GetFontCount())
    return nullptr;
  ComPtr<IDWriteFontFaceReference> reference;
  ComPtr<IDWriteFontFace3> face;
  if (FAILED(filtered->GetFontFaceReference(0, &reference)) || FAILED(reference->CreateFontFace(&face)))
    return nullptr;
  UINT32 count = 0;
  ComPtr<IDWriteFontFile> file;
  ComPtr<IDWriteFontFileLoader> loader;
  ComPtr<IDWriteLocalFontFileLoader> local_loader;
  if (FAILED(face->GetFiles(&count, nullptr)) || count != 1 || FAILED(face->GetFiles(&count, file.GetAddressOf())) || FAILED(file->GetLoader(&loader)) || FAILED(loader.As(&local_loader)))
    return nullptr;
  const void* key = nullptr;
  UINT32 key_size = 0;
  UINT32 path_length = 0;
  if (FAILED(file->GetReferenceKey(&key, &key_size)) || FAILED(local_loader->GetFilePathLengthFromKey(key, key_size, &path_length)))
    return nullptr;
  std::unique_ptr<wchar_t[]> path =
      std::make_unique<wchar_t[]>(static_cast<std::size_t>(path_length) + 1);
  if (FAILED(local_loader->GetFilePathFromKey(key, key_size, path.get(), path_length + 1)))
    return nullptr;
  return CreateFromFile(FromWide(path.get(), path_length), face->GetIndex());
}

std::shared_ptr<FontFace> FontManager::DefaultFont(const FontStyle& style) const {
  const std::unique_ptr<wchar_t[]> locale = ToWide(impl_->locale);
  if (impl_->fallback)
    return impl_->Fallback(nullptr, NativeStyle(style), locale.get(), 32);
  std::shared_ptr<FontFace> default_face = MatchFamily(impl_->default_family, style);
  if (default_face)
    return default_face;
  ComPtr<IDWriteFontFamily> family;
  ComPtr<IDWriteFont> font;
  ComPtr<IDWriteFontFace> face;
  if (FAILED(impl_->collection->GetFontFamily(0, &family)) || FAILED(FirstMatchingFontWithoutSimulations(family.Get(), NativeStyle(style), &font)) || FAILED(font->CreateFontFace(&face)))
    return nullptr;
  return impl_->MakeFace(face.Get(), font.Get(), family.Get());
}

// DWriteFontTypeface::MakeFromStream: keep the custom collection and search the
// first non-simulated font with exactly the requested TTC index.
std::shared_ptr<FontFace> FontManager::CreateFromData(
    std::span<const std::uint8_t> bytes, std::uint32_t collection_index) const {
  auto data = std::make_shared<FontFileData>();
  data->size = bytes.size();
  data->bytes = std::make_unique<std::uint8_t[]>(data->size);
  if (!bytes.empty())
    std::memcpy(data->bytes.get(), bytes.data(), bytes.size());
  auto loaders = std::make_shared<FontCollectionLoaders>(impl_->factory.Get());
  ComPtr<IDWriteFontCollection> collection;
  if (FAILED(loaders->Initialize(data)) || FAILED(impl_->factory->CreateCustomFontCollection(loaders->CollectionLoader(), nullptr, 0, &collection)))
    return nullptr;
  for (UINT32 i = 0; i < collection->GetFontFamilyCount(); ++i) {
    ComPtr<IDWriteFontFamily> family;
    if (FAILED(collection->GetFontFamily(i, &family)))
      return nullptr;
    for (UINT32 j = 0; j < family->GetFontCount(); ++j) {
      ComPtr<IDWriteFont> font;
      if (FAILED(family->GetFont(j, &font)))
        return nullptr;
      if (font->GetSimulations() != DWRITE_FONT_SIMULATIONS_NONE)
        continue;
      ComPtr<IDWriteFontFace> face;
      if (FAILED(font->CreateFontFace(&face)))
        return nullptr;
      if (face->GetIndex() != collection_index)
        continue;
      return impl_->MakeFace(face.Get(), font.Get(), family.Get(), std::move(loaders));
    }
  }
  return nullptr;
}

std::shared_ptr<FontFace> FontManager::CreateFromFile(
    const String& path, std::uint32_t collection_index) const {
  const std::unique_ptr<wchar_t[]> wide = ToWide(path);
  const HANDLE file = CreateFileW(wide.get(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return nullptr;
  struct FileCloser {
    HANDLE file;
    ~FileCloser() {
      CloseHandle(file);
    }
  } closer{file};
  LARGE_INTEGER length{};
  if (!GetFileSizeEx(file, &length) || length.QuadPart < 0 || static_cast<ULONGLONG>(length.QuadPart) > std::numeric_limits<std::size_t>::max())
    return nullptr;
  const std::size_t data_size = static_cast<std::size_t>(length.QuadPart);
  std::unique_ptr<std::uint8_t[]> data =
      std::make_unique<std::uint8_t[]>(data_size);
  std::size_t position = 0;
  while (position < data_size) {
    const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
        data_size - position,
        std::numeric_limits<DWORD>::max()));
    DWORD read = 0;
    if (!ReadFile(file, data.get() + position, request, &read, nullptr) || !read)
      return nullptr;
    position += read;
  }
  return CreateFromData(std::span<const std::uint8_t>(data.get(), data_size),
                        collection_index);
}

} // namespace blink
