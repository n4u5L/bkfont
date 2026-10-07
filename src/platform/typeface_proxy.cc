// Ported from: skia/src/ports/SkTypeface_proxy.cpp

#include "typeface_proxy.h"

#include <utility>

#include "base/immediate_crash.h"
#include "paint/picture.h"

namespace bkit {

TypefaceProxy::TypefaceProxy(std::shared_ptr<Typeface> real_typeface,
                             const FontStyle& style, bool is_fixed_pitch)
    : Typeface(style, is_fixed_pitch),
      real_typeface_(std::move(real_typeface)) {
  // SkASSERT_RELEASE(fRealTypeface).
  if (!real_typeface_) {
    base::ImmediateCrash();
  }
}

int TypefaceProxy::OnGetUPEM() const {
  return real_typeface_->GetUnitsPerEm();
}

std::unique_ptr<StreamAsset> TypefaceProxy::OnOpenStream(int* ttc_index) const {
  return real_typeface_->OnOpenStream(ttc_index);
}

std::shared_ptr<Typeface> TypefaceProxy::OnMakeClone(const FontArguments& args) const {
  return real_typeface_->OnMakeClone(args);
}

bool TypefaceProxy::OnGlyphMaskNeedsCurrentColor() const {
  return real_typeface_->GlyphMaskNeedsCurrentColor();
}

int TypefaceProxy::OnGetVariationDesignPosition(std::span<FontArguments::VariationPosition::Coordinate> coordinates) const {
  return real_typeface_->OnGetVariationDesignPosition(coordinates);
}

int TypefaceProxy::OnGetVariationDesignParameters(std::span<FontParameters::Variation::Axis> parameters) const {
  return real_typeface_->OnGetVariationDesignParameters(parameters);
}

FontStyle TypefaceProxy::OnGetFontStyle() const {
  return real_typeface_->OnGetFontStyle();
}

bool TypefaceProxy::OnGetFixedPitch() const {
  return real_typeface_->OnGetFixedPitch();
}

void TypefaceProxy::OnGetFamilyName(String* family_name) const {
  real_typeface_->OnGetFamilyName(family_name);
}

bool TypefaceProxy::OnGetPostScriptName(String* post_script_name) const {
  return real_typeface_->GetPostScriptName(post_script_name);
}

std::unique_ptr<Typeface::LocalizedStrings> TypefaceProxy::OnCreateFamilyNameIterator() const {
  return real_typeface_->CreateFamilyNameIterator();
}

int TypefaceProxy::OnGetTableTags(std::span<std::uint32_t> tags) const {
  return real_typeface_->ReadTableTags(tags);
}

std::size_t TypefaceProxy::OnGetTableData(std::uint32_t tag, std::size_t offset, std::size_t length, void* data) const {
  return real_typeface_->GetTableData(tag, offset, length, data);
}

std::unique_ptr<ScalerContext> TypefaceProxy::OnCreateScalerContext(const ScalerContextRec& rec) const {
  TypefaceProxy* proxy = const_cast<TypefaceProxy*>(this);
  return std::make_unique<ScalerContextProxy>(
      real_typeface_->OnCreateScalerContextAsProxyTypeface(rec, proxy),
      *proxy,
      rec);
}

void TypefaceProxy::OnFilterRec(ScalerContextRec* rec) const {
  real_typeface_->OnFilterRec(rec);
}

void TypefaceProxy::OnCharsToGlyphs(std::span<const std::int32_t> chars, std::span<std::uint16_t> glyphs) const {
  real_typeface_->UnicharsToGlyphs(chars, glyphs);
}

int TypefaceProxy::OnCountGlyphs() const {
  return real_typeface_->CountGlyphs();
}

ScalerContextProxy::ScalerContextProxy(std::unique_ptr<ScalerContext> real_scaler_context,
                                       TypefaceProxy& proxy_typeface,
                                       const ScalerContextRec& rec)
    : ScalerContext(std::const_pointer_cast<Typeface>(proxy_typeface.shared_from_this()), rec),
      real_scaler_context_(std::move(real_scaler_context)) {
}

ScalerContext::GlyphMetrics ScalerContextProxy::GenerateMetrics(const PlatformGlyph& glyph, Arena* arena) {
  return real_scaler_context_->GenerateMetrics(glyph, arena);
}

void ScalerContextProxy::GenerateImage(const PlatformGlyph& glyph, void* image_buffer) {
  real_scaler_context_->GenerateImage(glyph, image_buffer);
}

std::optional<ScalerContext::GeneratedPath> ScalerContextProxy::GeneratePath(const PlatformGlyph& glyph) {
  return real_scaler_context_->GeneratePath(glyph);
}

std::shared_ptr<Drawable> ScalerContextProxy::GenerateDrawable(const PlatformGlyph& glyph) {
  return real_scaler_context_->GenerateDrawable(glyph);
}

void ScalerContextProxy::GenerateFontMetrics(PlatformFontMetrics* metrics) {
  real_scaler_context_->GenerateFontMetrics(metrics);
}

} // namespace bkit
