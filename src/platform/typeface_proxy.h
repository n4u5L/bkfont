// Ported from: skia/src/ports/SkTypeface_proxy.h

#pragma once

#include <memory>

#include "scaler_context.h"
#include "typeface.h"

namespace bkit {

class TypefaceProxy;

// SkScalerContext_proxy.
class ScalerContextProxy : public ScalerContext {
public:
  ScalerContextProxy(std::unique_ptr<ScalerContext> real_scaler_context,
                     TypefaceProxy& proxy_typeface,
                     const ScalerContextRec& rec);

  ~ScalerContextProxy() override = default;

protected:
  GlyphMetrics GenerateMetrics(const PlatformGlyph& glyph, Arena* arena) override;
  void GenerateImage(const PlatformGlyph& glyph, void* image_buffer) override;
  std::optional<GeneratedPath> GeneratePath(const PlatformGlyph& glyph) override;
  std::shared_ptr<Drawable> GenerateDrawable(const PlatformGlyph& glyph) override;
  void GenerateFontMetrics(PlatformFontMetrics* metrics) override;

private:
  std::unique_ptr<ScalerContext> real_scaler_context_;
};

// SkTypeface_proxy.
class TypefaceProxy : public Typeface {
public:
  TypefaceProxy(std::shared_ptr<Typeface> real_typeface,
                const FontStyle& style, bool is_fixed_pitch = false);

protected:
  int OnGetUPEM() const override;
  std::unique_ptr<StreamAsset> OnOpenStream(int* ttc_index) const override;
  std::shared_ptr<Typeface> OnMakeClone(const FontArguments& args) const override;
  bool OnGlyphMaskNeedsCurrentColor() const override;
  int OnGetVariationDesignPosition(std::span<FontArguments::VariationPosition::Coordinate> coordinates) const override;
  int OnGetVariationDesignParameters(std::span<FontParameters::Variation::Axis> parameters) const override;
  FontStyle OnGetFontStyle() const override;
  bool OnGetFixedPitch() const override;
  void OnGetFamilyName(String* family_name) const override;
  bool OnGetPostScriptName(String* post_script_name) const override;
  std::unique_ptr<LocalizedStrings> OnCreateFamilyNameIterator() const override;
  int OnGetTableTags(std::span<std::uint32_t> tags) const override;
  std::size_t OnGetTableData(std::uint32_t tag, std::size_t offset, std::size_t length, void* data) const override;
  std::unique_ptr<ScalerContext> OnCreateScalerContext(const ScalerContextRec& rec) const override;
  void OnFilterRec(ScalerContextRec* rec) const override;
  void OnCharsToGlyphs(std::span<const std::int32_t> chars, std::span<std::uint16_t> glyphs) const override;
  int OnCountGlyphs() const override;

private:
  std::shared_ptr<Typeface> real_typeface_;
};

} // namespace bkit
