// Ported from: blink/renderer/platform/fonts/shaping/harfbuzz_face.cc

/*
 * Copyright (c) 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "harfbuzz_face.h"
#include <cmath>
#include <limits>
#include <span>
#include <hb-ot.h>
#include "base/allocator/partitions.h"
#include "font/color_table_lookup.h"
#include "font/font_platform_data.h"
#include "font/font_metrics.h"
#include "harfbuzz_face_from_typeface.h"
#include "harfbuzz_font_data.h"
#include "harfbuzz_font_cache.h"
#include "platform/typeface.h"
#include "runtime_enabled_features.h"
#include "text/character.h"
#include "base/text/character_names.h"
#include "base/vector.h"
#include "base/math_extras.h"
#include "font/text_metrics.h"

namespace bkit {

namespace {

VariationSelectorMode& VariationMode() {
  static thread_local VariationSelectorMode mode = kUseSpecifiedVariationSelector;
  return mode;
}

} // namespace

VariationSelectorMode HarfBuzzFace::GetVariationSelectorMode() {
  return VariationMode();
}

void HarfBuzzFace::SetVariationSelectorMode(VariationSelectorMode value) {
  VariationMode() = value;
}

static hb_bool_t HarfBuzzGetGlyph(hb_font_t* hb_font,
                                  void* font_data,
                                  hb_codepoint_t unicode,
                                  hb_codepoint_t variation_selector,
                                  hb_codepoint_t* glyph,
                                  void* user_data) {
  HarfBuzzFontData* hb_font_data =
      reinterpret_cast<HarfBuzzFontData*>(font_data);

  if (hb_font_data->range_set_ && !hb_font_data->range_set_->Contains(unicode)) {
    return false;
  }

  // If the system fonts do not have a glyph coverage for line separator
  // (0x2028) or paragraph separator (0x2029), missing glyph would be displayed
  // in Chrome instead. If the system font has l-sep and p-sep symbols for the
  // 0x2028 and 0x2029 codepoints, they will be displayed, see
  // https://crbug.com/550275. To prevent that, we are replacing line and
  // paragraph separators with space, as it is said in unicode specification,
  // compare: https://www.unicode.org/faq/unsup_char.html#2.
  if (unicode == uchar::kLineSeparator || unicode == uchar::kParagraphSeparator) {
    unicode = uchar::kSpace;
  }

  bool consider_variation_selector = false;
  bool is_variation_sequence = false;

  // Emoji System Fonts on Mac, Win and Android either do not have cmap 14
  // subtable or it does not include all emojis from their cmap table. We use
  // cmap 14 subtable to identify whether there is a colored (emoji
  // presentation) or a monochromatic (text presentation) glyph in the font.
  // This may lead to the cases when we will not be able to get the glyph ID
  // for the requested variation sequence using fallback system font and will
  // continue the second shaping fallback list pass ignoring variation
  // selectors and may end up using web font with wrong emoji presentation
  // instead of using system font with the correct presentation. To prevent that
  // once we reached system fallback fonts, we can ignore emoji variation
  // selectors since we will get the font with the correct presentation relying
  // on FontFallbackPriority in `FontCache::PlatformFallbackFontForCharacter`.
  VariationSelectorMode variation_selector_mode =
      HarfBuzzFace::GetVariationSelectorMode();
  if (!ShouldIgnoreVariationSelector(variation_selector_mode) && Character::IsUnicodeVariationSelector(variation_selector) && Character::IsVariationSequence(unicode, variation_selector)) {
    is_variation_sequence = true;
    consider_variation_selector = true;
  } else if (UseFontVariantEmojiVariationSelector(variation_selector_mode) && Character::IsEmoji(unicode)) {
    consider_variation_selector = true;
  }

  bool text_presentation_requested = false;
  bool emoji_presentation_requested = false;

  // Variation sequences are a special case because we want to distinguish
  // between the cases when we found a glyph for the whole variation sequence in
  // cmap format 14 subtable and when we found only a base character of the
  // variation sequence. In the latter case we set the glyph value to
  // `kUnmatchedVSGlyphId`.
  if (consider_variation_selector) {
    if (!is_variation_sequence) {
      if (variation_selector_mode == kForceVariationSelector15 || (variation_selector_mode == kUseUnicodeDefaultPresentation && Character::IsEmojiTextDefault(unicode))) {
        variation_selector = uchar::kVariationSelector15;
      } else if (variation_selector_mode == kForceVariationSelector16 || (variation_selector_mode == kUseUnicodeDefaultPresentation && Character::IsEmojiEmojiDefault(unicode))) {
        variation_selector = uchar::kVariationSelector16;
      }
    }

    text_presentation_requested =
        (variation_selector == uchar::kVariationSelector15);
    emoji_presentation_requested =
        (variation_selector == uchar::kVariationSelector16);

    hb_bool_t hb_has_vs_glyph = hb_font_get_variation_glyph(
        hb_font_get_parent(hb_font),
        unicode,
        variation_selector,
        glyph);
    if (hb_has_vs_glyph) {
      // Found a glyph for variation sequence, no need to look for a base
      // character, can just return.
      return true;
    }
    // Unable to find a glyph for variation sequence, now we need to look
    // for a glyph for the base character from variation sequence.
    variation_selector = 0;
  }

  hb_bool_t hb_has_base_glyph = hb_font_get_glyph(
      hb_font_get_parent(hb_font),
      unicode,
      variation_selector,
      glyph);

  if (consider_variation_selector && hb_has_base_glyph) {
    // Unable to find a glyph for variation sequence, but found a glyph for
    // the base character from variation sequence ignoring variation selector.
    // We use `TypefaceHasAnySupportedColorTable` to check whether a typeface
    // has colored table and based on that we make an assumption whether a font
    // has a colored or monochromatic glyph for base character from variation
    // sequence. We set `glyph` to `kUnmatchedVSGlyphId` only when font has a
    // wrong presentation for base character.
    if (RuntimeEnabledFeatures::SystemFallbackEmojiVSSupportEnabled() && (text_presentation_requested || emoji_presentation_requested)) {
      const Typeface* typeface = hb_font_data->font_.GetTypeface().get();
      // TODO(https://bugs.skia.org/374078818): Ideally we also want to check
      // weather the base codepoint is present in the found color table,
      // requested API from Skia.
      bool has_color_table =
          ColorTableLookup::TypefaceHasAnySupportedColorTable(typeface);
      if ((has_color_table && text_presentation_requested) || (!has_color_table && emoji_presentation_requested)) {
        *glyph = kUnmatchedVSGlyphId;
      }
    } else {
      *glyph = kUnmatchedVSGlyphId;
    }
  }

  // MacOS CoreText API synthesizes GlyphID for several unicode codepoints,
  // for example, hyphens and separators for some fonts. HarfBuzz does not
  // synthesize such glyphs, and as it's not found from the last resort font, we
  // end up with displaying tofu, see https://crbug.com/1267606 for details.
  // Chrome uses Times as last resort fallback font and in Times the only visible
  // synthesizing characters are hyphen (0x2010) and non-breaking hyphen (0x2011).
  // For performance reasons, we limit this fallback lookup to the specific
  // missing glyphs for hyphens and only to Mac OS, where we're facing this issue.
  return hb_has_base_glyph;
}

static hb_bool_t HarfBuzzGetNominalGlyph(hb_font_t* font, void* data,
                                         hb_codepoint_t unicode, hb_codepoint_t* glyph, void* user) {
  return HarfBuzzGetGlyph(font, data, unicode, 0, glyph, user);
}

static hb_position_t HarfBuzzGetGlyphHorizontalAdvance(hb_font_t*,
                                                       void* font_data,
                                                       hb_codepoint_t glyph,
                                                       void*) {
  HarfBuzzFontData* hb_font_data =
      reinterpret_cast<HarfBuzzFontData*>(font_data);
  hb_position_t advance = 0;

  FontGetGlyphWidthForHarfBuzz(hb_font_data->font_, glyph, &advance);
  return advance;
}

static void HarfBuzzGetGlyphHorizontalAdvances(
    hb_font_t* font,
    void* font_data,
    unsigned count,
    const hb_codepoint_t* first_glyph,
    unsigned int glyph_stride,
    hb_position_t* first_advance,
    unsigned int advance_stride,
    void* user_data) {
  HarfBuzzFontData* hb_font_data =
      reinterpret_cast<HarfBuzzFontData*>(font_data);
  FontGetGlyphWidthForHarfBuzz(hb_font_data->font_, count, first_glyph,
                               glyph_stride, first_advance, advance_stride);
}

static hb_bool_t HarfBuzzGetGlyphVerticalOrigin(hb_font_t* hb_font,
                                                void* font_data,
                                                hb_codepoint_t glyph,
                                                hb_position_t* x,
                                                hb_position_t* y,
                                                void* user_data) {
  HarfBuzzFontData* hb_font_data =
      reinterpret_cast<HarfBuzzFontData*>(font_data);
  OpenTypeVerticalData* vertical_data = hb_font_data->VerticalData();
  if (!vertical_data) {
    return false;
  }

  float result[] = {0, 0};
  Glyph the_glyph = static_cast<Glyph>(glyph);
  vertical_data->GetVerticalTranslationsForGlyphs(hb_font_data->font_,
                                                  &the_glyph, 1, result);
  *x = ScalarToHarfBuzzPosition(-result[0]);
  *y = ScalarToHarfBuzzPosition(-result[1]);
  return true;
}

static hb_position_t HarfBuzzGetGlyphVerticalAdvance(hb_font_t* hb_font,
                                                     void* font_data,
                                                     hb_codepoint_t glyph,
                                                     void* user_data) {
  HarfBuzzFontData* hb_font_data =
      reinterpret_cast<HarfBuzzFontData*>(font_data);
  OpenTypeVerticalData* vertical_data = hb_font_data->VerticalData();
  if (!vertical_data) {
    return ScalarToHarfBuzzPosition(hb_font_data->height_fallback_);
  }

  Glyph the_glyph = static_cast<Glyph>(glyph);
  float advance_height = -vertical_data->AdvanceHeight(the_glyph);
  return ScalarToHarfBuzzPosition(advance_height);
}

static hb_bool_t HarfBuzzGetGlyphExtents(hb_font_t* hb_font,
                                         void* font_data,
                                         hb_codepoint_t glyph,
                                         hb_glyph_extents_t* extents,
                                         void* user_data) {
  HarfBuzzFontData* hb_font_data =
      reinterpret_cast<HarfBuzzFontData*>(font_data);

  FontGetGlyphExtentsForHarfBuzz(hb_font_data->font_, glyph, extents);
  return true;
}

namespace {

hb_font_funcs_t* FontFunctions() {
  static hb::unique_ptr<hb_font_funcs_t> functions([] {
    hb_font_funcs_t* f = hb_font_funcs_create();
    hb_font_funcs_set_glyph_h_advance_func(f, HarfBuzzGetGlyphHorizontalAdvance, nullptr, nullptr);
    hb_font_funcs_set_glyph_h_advances_func(f, HarfBuzzGetGlyphHorizontalAdvances, nullptr, nullptr);
    hb_font_funcs_set_variation_glyph_func(f, HarfBuzzGetGlyph, nullptr, nullptr);
    hb_font_funcs_set_nominal_glyph_func(f, HarfBuzzGetNominalGlyph, nullptr, nullptr);
    hb_font_funcs_set_glyph_v_advance_func(f, HarfBuzzGetGlyphVerticalAdvance, nullptr, nullptr);
    hb_font_funcs_set_glyph_v_origin_func(f, HarfBuzzGetGlyphVerticalOrigin, nullptr, nullptr);
    hb_font_funcs_set_glyph_extents_func(f, HarfBuzzGetGlyphExtents, nullptr, nullptr);
    hb_font_funcs_make_immutable(f);
    return f;
  }());
  return functions.get();
}

} // namespace

HarfBuzzFace::HarfBuzzFace(const FontPlatformData* platform_data, uint64_t unique_id)
    : platform_data_(platform_data),
      harfbuzz_font_data_(HarfBuzzFontCache::Get().GetOrCreate(
          unique_id ? unique_id : platform_data->UniqueID(), platform_data)) {
}

static hb_blob_t* HarfBuzzGetTable(hb_face_t* face,
                                       hb_tag_t tag,
                                       void* user_data) {
  const auto& typeface = *static_cast<std::shared_ptr<Typeface>*>(user_data);

  const wtf_size_t table_size =
      base::checked_cast<wtf_size_t>(typeface->GetTableSize(tag));
  if (!table_size) {
    return nullptr;
  }

  char* buffer = reinterpret_cast<char*>(Partitions::FastMalloc(
      table_size, "HarfBuzzFontData"));
  if (!buffer) {
    return nullptr;
  }
  std::size_t actual_size = typeface->GetTableData(tag, 0, table_size, buffer);
  if (table_size != actual_size) {
    Partitions::FastFree(buffer);
    return nullptr;
  }
  return hb_blob_create(const_cast<char*>(buffer), table_size,
                        HB_MEMORY_MODE_WRITABLE, buffer, Partitions::FastFree);
}

// The table callback borrows the typeface; the returned face owns a strong
// reference to it, as the GC keeps the SkTypeface alive upstream.
static hb::unique_ptr<hb_face_t> CreateFace(
    const FontPlatformData* platform_data) {
  hb::unique_ptr<hb_face_t> face;

  std::shared_ptr<Typeface> typeface = platform_data->Typeface();
  // The IS_APPLE exclusion only concerns CoreText typefaces.
  face = HbFaceFromTypeface(typeface);

  // Fallback to table copies if there is no in-memory access.
  if (!face) {
    auto* owner = new std::shared_ptr<Typeface>(typeface);
    face = hb::unique_ptr<hb_face_t>(hb_face_create_for_tables(
        HarfBuzzGetTable, owner,
        [](void* value) { delete static_cast<std::shared_ptr<Typeface>*>(value); }));
  }

  return face;
}

namespace {

std::shared_ptr<HarfBuzzFontData> CreateHarfBuzzFontData(hb_face_t* face,
                                                         Typeface* typeface) {
  hb::unique_ptr<hb_font_t> ot_font(hb_font_create(face));
  hb_ot_font_set_funcs(ot_font.get());

  int axis_count = typeface->GetVariationDesignPosition({});
  if (axis_count > 0) {
    Vector<FontArguments::VariationPosition::Coordinate> axis_values;
    axis_values.resize(static_cast<wtf_size_t>(axis_count));
    if (typeface->GetVariationDesignPosition(std::span(axis_values.data(), axis_values.size())) > 0) {
      hb_font_set_variations(
          ot_font.get(), reinterpret_cast<hb_variation_t*>(axis_values.data()),
          axis_values.size());
    }
  }

  // Creating a sub font means that non-available functions
  // are found from the parent.
  hb_font_t* const unscaled_font = hb_font_create_sub_font(ot_font.get());
  auto data = std::make_shared<HarfBuzzFontData>(unscaled_font);
  hb_font_set_funcs(unscaled_font, FontFunctions(), data.get(), nullptr);
  return data;
}

} // namespace

std::shared_ptr<HarfBuzzFontData> HarfBuzzFontCache::GetOrCreate(
    uint64_t unique_id, const FontPlatformData* platform_data) {
  // GC removes dead weak values upstream. In this non-GC port, remove them
  // at the next cache access; live entries retain the original sharing.
  font_map_.erase_if([](const auto& entry) { return entry.value.expired(); });
  const auto result = font_map_.insert(unique_id, std::weak_ptr<HarfBuzzFontData>{});
  if (auto existing = result.stored_value->value.lock()) return existing;

  hb::unique_ptr<hb_face_t> face = CreateFace(platform_data);
  std::shared_ptr<HarfBuzzFontData> data =
      CreateHarfBuzzFontData(face.get(), platform_data->Typeface().get());

  result.stored_value->value = data;
  return data;
}

static_assert(
    std::is_same<decltype(FontArguments::VariationPosition::Coordinate::axis),
                 decltype(hb_variation_t::tag)>::value &&
        std::is_same<
            decltype(FontArguments::VariationPosition::Coordinate::value),
            decltype(hb_variation_t::value)>::value &&
        sizeof(FontArguments::VariationPosition::Coordinate) ==
            sizeof(hb_variation_t),
    "Typeface and HarfBuzz Variation parameter types must match in structure "
    "and size.");

HarfBuzzFace::~HarfBuzzFace() = default;

static inline bool TableHasSpace(hb_face_t* face,
                                 hb_set_t* glyphs,
                                 hb_tag_t tag,
                                 hb_codepoint_t space) {
  unsigned count = hb_ot_layout_table_get_lookup_count(face, tag);
  for (unsigned i = 0; i < count; i++) {
    hb_ot_layout_lookup_collect_glyphs(face, tag, i, glyphs, glyphs, glyphs, nullptr);
    if (hb_set_has(glyphs, space)) {
      return true;
    }
  }
  return false;
}

static bool GetSpaceGlyph(hb_font_t* font, hb_codepoint_t& space) {
  return hb_font_get_nominal_glyph(font, uchar::kSpace, &space);
}

bool HarfBuzzFace::HasSpaceInLigaturesOrKerning(TypesettingFeatures features) {
  const hb_codepoint_t kInvalidCodepoint = static_cast<hb_codepoint_t>(-1);
  hb_codepoint_t space = kInvalidCodepoint;

  hb::unique_ptr<hb_set_t> glyphs(hb_set_create());

  hb_font_t* unscaled_font = harfbuzz_font_data_->unscaled_font_.get();

  // Check whether computing is needed and compute for gpos/gsub.
  if (features & kKerning && harfbuzz_font_data_->space_in_gpos_ == HarfBuzzFontData::SpaceGlyphInOpenTypeTables::kUnknown) {
    if (space == kInvalidCodepoint && !GetSpaceGlyph(unscaled_font, space)) {
      return false;
    }
    // Compute for gpos.
    hb_face_t* face = hb_font_get_face(unscaled_font);

    harfbuzz_font_data_->space_in_gpos_ =
        hb_ot_layout_has_positioning(face) && TableHasSpace(face, glyphs.get(), HB_OT_TAG_GPOS, space)
            ? HarfBuzzFontData::SpaceGlyphInOpenTypeTables::kPresent
            : HarfBuzzFontData::SpaceGlyphInOpenTypeTables::kNotPresent;
  }

  hb_set_clear(glyphs.get());

  if (features & kLigatures && harfbuzz_font_data_->space_in_gsub_ == HarfBuzzFontData::SpaceGlyphInOpenTypeTables::kUnknown) {
    if (space == kInvalidCodepoint && !GetSpaceGlyph(unscaled_font, space)) {
      return false;
    }
    // Compute for gpos.
    hb_face_t* face = hb_font_get_face(unscaled_font);

    harfbuzz_font_data_->space_in_gsub_ =
        hb_ot_layout_has_substitution(face) && TableHasSpace(face, glyphs.get(), HB_OT_TAG_GSUB, space)
            ? HarfBuzzFontData::SpaceGlyphInOpenTypeTables::kPresent
            : HarfBuzzFontData::SpaceGlyphInOpenTypeTables::kNotPresent;
  }

  return (features & kKerning && harfbuzz_font_data_->space_in_gpos_ == HarfBuzzFontData::SpaceGlyphInOpenTypeTables::kPresent) || (features & kLigatures && harfbuzz_font_data_->space_in_gsub_ == HarfBuzzFontData::SpaceGlyphInOpenTypeTables::kPresent);
}

unsigned HarfBuzzFace::UnitsPerEmFromHeadTable() {
  return hb_face_get_upem(hb_font_get_face(harfbuzz_font_data_->unscaled_font_.get()));
}

Glyph HarfBuzzFace::HbGlyphForCharacter(UChar32 character) {
  hb_codepoint_t glyph = 0;
  HarfBuzzGetNominalGlyph(harfbuzz_font_data_->unscaled_font_.get(),
                          harfbuzz_font_data_.get(),
                          character,
                          &glyph,
                          nullptr);
  return static_cast<Glyph>(glyph);
}

hb_codepoint_t HarfBuzzFace::HarfBuzzGetGlyphForTesting(UChar32 character, UChar32 selector) {
  hb_codepoint_t glyph = 0;
  HarfBuzzGetGlyph(harfbuzz_font_data_->unscaled_font_.get(),
                   harfbuzz_font_data_.get(),
                   character,
                   selector,
                   &glyph,
                   nullptr);
  return glyph;
}

bool HarfBuzzFace::ShouldSubpixelPosition() {
  return harfbuzz_font_data_->font_.IsSubpixel();
}

const OpenTypeVerticalData& HarfBuzzFace::VerticalData() const {
  harfbuzz_font_data_->UpdateFallbackMetricsAndScale(*platform_data_, kPrepareForVerticalLayout);
  return *harfbuzz_font_data_->VerticalData();
}

hb_font_t* HarfBuzzFace::GetScaledFont(const UnicodeRangeSet* range_set,
                                       VerticalLayoutCallbacks vertical_layout,
                                       float specified_size) const {
  harfbuzz_font_data_->range_set_ = range_set
                                        ? std::make_unique<const UnicodeRangeSet>(*range_set)
                                        : nullptr;
  harfbuzz_font_data_->UpdateFallbackMetricsAndScale(*platform_data_,
                                                     vertical_layout);

  int scale = ScalarToHarfBuzzPosition(platform_data_->size());
  hb_font_t* unscaled_font = harfbuzz_font_data_->unscaled_font_.get();
  hb_font_set_scale(unscaled_font, scale, scale);
  // See contended discussion in https://github.com/harfbuzz/harfbuzz/pull/1484
  // Setting ptem here is critical for HarfBuzz to know where to lookup spacing
  // offset in the AAT trak table, the unit pt in ptem here means "CoreText"
  // points. After discussion on the pull request and with Apple developers, the
  // meaning of HarfBuzz' hb_font_set_ptem API was changed to expect the
  // equivalent of CSS pixels here.
  hb_font_set_ptem(unscaled_font, specified_size > 0 ? specified_size : platform_data_->size());

  return unscaled_font;
}

hb_font_t* HarfBuzzFace::GetScaledFont() const {
  return GetScaledFont(nullptr, HarfBuzzFace::kNoVerticalLayout, platform_data_->size());
}

void HarfBuzzFace::Init() {
  FontFunctions();
}

} // namespace bkit
