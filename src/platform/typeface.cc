// Ported from: skia/src/core/SkTypeface.cpp

#include "typeface.h"

#include <algorithm>
#include <utility>

#include "base/immediate_crash.h"
#include "platform_font.h"
#include "scaler_context.h"
#include "typeface_cache.h"

namespace bkit {

Typeface::Typeface(const FontStyle& style, bool is_fixed_pitch)
    : unique_id_(TypefaceCache::NewTypefaceID()),
      style_(style),
      is_fixed_pitch_(is_fixed_pitch) {
}

Typeface::~Typeface() = default;

namespace {

class EmptyTypeface final : public Typeface {
public:
  static std::shared_ptr<Typeface> Make() {
    static const std::shared_ptr<Typeface>& instance = *new std::shared_ptr<Typeface>(new EmptyTypeface);
    return instance;
  }

protected:
  EmptyTypeface()
      : Typeface(FontStyle(), true) {
  }

  std::unique_ptr<StreamAsset> OnOpenStream(int*) const override {
    return nullptr;
  }
  std::shared_ptr<Typeface> OnMakeClone(const FontArguments&) const override {
    return RefThis();
  }
  std::unique_ptr<ScalerContext> OnCreateScalerContext(const ScalerContextRec& rec) const override {
    return ScalerContext::MakeEmpty(RefThis(), rec);
  }
  void OnFilterRec(ScalerContextRec*) const override {
  }
  void OnCharsToGlyphs(std::span<const std::int32_t>, std::span<std::uint16_t> glyphs) const override {
    std::fill(glyphs.begin(), glyphs.end(), static_cast<std::uint16_t>(0));
  }
  int OnCountGlyphs() const override {
    return 0;
  }
  int OnGetUPEM() const override {
    return 0;
  }
  class EmptyLocalizedStrings final : public Typeface::LocalizedStrings {
  public:
    bool Next(Typeface::LocalizedString*) override {
      return false;
    }
  };
  void OnGetFamilyName(String* family_name) const override {
    *family_name = String();
  }
  bool OnGetPostScriptName(String*) const override {
    return false;
  }
  std::unique_ptr<Typeface::LocalizedStrings> OnCreateFamilyNameIterator() const override {
    return std::make_unique<EmptyLocalizedStrings>();
  }
  bool OnGlyphMaskNeedsCurrentColor() const override {
    return false;
  }
  int OnGetVariationDesignPosition(std::span<FontArguments::VariationPosition::Coordinate>) const override {
    return 0;
  }
  int OnGetVariationDesignParameters(std::span<FontParameters::Variation::Axis>) const override {
    return 0;
  }
  int OnGetTableTags(std::span<std::uint32_t>) const override {
    return 0;
  }
  std::size_t OnGetTableData(std::uint32_t, std::size_t, std::size_t, void*) const override {
    return 0;
  }
};

} // namespace

std::shared_ptr<Typeface> Typeface::MakeEmpty() {
  return EmptyTypeface::Make();
}

bool Typeface::Equal(const Typeface* facea, const Typeface* faceb) {
  if (facea == faceb) {
    return true;
  }
  if (!facea || !faceb) {
    return false;
  }
  return facea->UniqueID() == faceb->UniqueID();
}

std::shared_ptr<Typeface> Typeface::MakeClone(const FontArguments& args) const {
  return OnMakeClone(args);
}

bool Typeface::GlyphMaskNeedsCurrentColor() const {
  return OnGlyphMaskNeedsCurrentColor();
}

int Typeface::GetVariationDesignPosition(std::span<FontArguments::VariationPosition::Coordinate> coordinates) const {
  return OnGetVariationDesignPosition(coordinates);
}

int Typeface::GetVariationDesignParameters(std::span<FontParameters::Variation::Axis> parameters) const {
  return OnGetVariationDesignParameters(parameters);
}

int Typeface::CountTables() const {
  return OnGetTableTags({});
}

int Typeface::ReadTableTags(std::span<std::uint32_t> tags) const {
  return OnGetTableTags(tags);
}

std::size_t Typeface::GetTableSize(std::uint32_t tag) const {
  return OnGetTableData(tag, 0, ~0U, nullptr);
}

std::size_t Typeface::GetTableData(std::uint32_t tag, std::size_t offset, std::size_t length, void* data) const {
  return OnGetTableData(tag, offset, length, data);
}

std::shared_ptr<Data> Typeface::CopyTableData(std::uint32_t tag) const {
  return OnCopyTableData(tag);
}

std::shared_ptr<Data> Typeface::OnCopyTableData(std::uint32_t tag) const {
  std::size_t size = GetTableSize(tag);
  if (size) {
    std::shared_ptr<Data> data = Data::MakeUninitialized(size);
    (void)GetTableData(tag, 0, size, data->writable_data());
    return data;
  }
  return nullptr;
}

std::unique_ptr<StreamAsset> Typeface::OpenStream(int* ttc_index) const {
  int ttc_index_storage;
  if (nullptr == ttc_index) {
    ttc_index = &ttc_index_storage;
  }
  return OnOpenStream(ttc_index);
}

std::unique_ptr<ScalerContext> Typeface::CreateScalerContext(const ScalerContextRec& rec) const {
  std::unique_ptr<ScalerContext> scaler_context = OnCreateScalerContext(rec);
  return scaler_context;
}

std::unique_ptr<ScalerContext> Typeface::OnCreateScalerContextAsProxyTypeface(const ScalerContextRec&, Typeface*) const {
  // SK_ABORT("Not implemented.").
  base::ImmediateCrash();
}

void Typeface::UnicharsToGlyphs(std::span<const std::int32_t> uni, std::span<std::uint16_t> glyphs) const {
  if (const std::size_t n = std::min(uni.size(), glyphs.size())) {
    OnCharsToGlyphs(uni.first(n), glyphs.first(n));
  }
}

std::uint16_t Typeface::UnicharToGlyph(std::int32_t uni) const {
  std::uint16_t glyphs[1] = {0};
  OnCharsToGlyphs({&uni, 1}, glyphs);
  return glyphs[0];
}

int Typeface::CountGlyphs() const {
  return OnCountGlyphs();
}

int Typeface::GetUnitsPerEm() const {
  return OnGetUPEM();
}

std::unique_ptr<Typeface::LocalizedStrings> Typeface::CreateFamilyNameIterator() const {
  return OnCreateFamilyNameIterator();
}

String Typeface::GetFamilyName() const {
  String name;
  OnGetFamilyName(&name);
  return name;
}

bool Typeface::GetPostScriptName(String* name) const {
  return OnGetPostScriptName(name);
}

FontStyle Typeface::GetFontStyle() const {
  return OnGetFontStyle();
}

FontStyle Typeface::OnGetFontStyle() const {
  return style_;
}

bool Typeface::IsBold() const {
  return OnGetFontStyle().GetWeight() >= FontStyle::kSemiBold_Weight;
}

bool Typeface::IsItalic() const {
  return OnGetFontStyle().GetSlant() != FontStyle::kUpright_Slant;
}

bool Typeface::IsFixedPitch() const {
  return OnGetFixedPitch();
}

bool Typeface::OnGetFixedPitch() const {
  return is_fixed_pitch_;
}

ScalarRect Typeface::GetBounds() const {
  bounds_once_([this] {
    if (!OnComputeBounds(&bounds_)) {
      bounds_ = ScalarRect();
    }
  });
  return bounds_;
}

bool Typeface::OnComputeBounds(ScalarRect* bounds) const {
  // we use a big size to ensure lots of significant bits from the
  // scalercontext. then we scale back down to return our final answer (at
  // 1-pt)
  const float text_size = 2048;
  const float inv_text_size = 1 / text_size;

  PlatformFont font;
  font.SetTypeface(RefThis());
  font.SetSize(text_size);
  font.SetLinearMetrics(true);

  ScalerContextRec rec;
  ScalerContext::MakeRecAndEffectsFromFont(font, &rec);

  std::unique_ptr<ScalerContext> ctx = CreateScalerContext(rec);

  PlatformFontMetrics fm;
  ctx->GetFontMetrics(&fm);
  if (!fm.HasBounds()) {
    return false;
  }
  *bounds = ScalarRect::MakeLTRB(fm.x_min * inv_text_size, fm.top * inv_text_size,
                                 fm.x_max * inv_text_size, fm.bottom * inv_text_size);
  return true;
}

} // namespace bkit
