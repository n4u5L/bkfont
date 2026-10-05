// Ported from: skia/src/text/GlyphRun.cpp

#include "glyph_run.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include "matrix.h"
#include "platform_paint.h"
#include "strike_spec.h"

namespace bkfont {

// -- GlyphRun -----------------------------------------------------------------

GlyphRun::GlyphRun(const PlatformFont& font,
                   std::span<const ScalarPoint> positions,
                   std::span<const std::uint16_t> glyph_ids,
                   std::span<const char> text,
                   std::span<const std::uint32_t> clusters,
                   std::span<const ScalarPoint> scaled_rotations)
    : glyph_ids_{glyph_ids.first(std::min(glyph_ids.size(), positions.size()))},
      positions_{positions.first(std::min(glyph_ids.size(), positions.size()))},
      text_{text},
      clusters_{clusters},
      scaled_rotations_{scaled_rotations},
      font_{font} {
}

GlyphRun::GlyphRun(const GlyphRun& that, const PlatformFont& font)
    : glyph_ids_{that.glyph_ids_},
      positions_{that.positions_},
      text_{that.text_},
      clusters_{that.clusters_},
      font_{font} {
}

// -- GlyphRunList -------------------------------------------------------------

GlyphRunList::GlyphRunList(const TextBlob* blob,
                           ScalarRect bounds,
                           ScalarPoint origin,
                           std::span<const GlyphRun> glyph_run_list,
                           GlyphRunBuilder* builder)
    : glyph_runs_{glyph_run_list},
      original_text_blob_{blob},
      source_bounds_{bounds},
      origin_{origin},
      builder_{builder} {
}

GlyphRunList::GlyphRunList(const GlyphRun& glyph_run,
                           const ScalarRect& bounds,
                           ScalarPoint origin,
                           GlyphRunBuilder* builder)
    : glyph_runs_{std::span<const GlyphRun>{&glyph_run, 1}},
      original_text_blob_{nullptr},
      source_bounds_{bounds},
      origin_{origin},
      builder_{builder} {
}

std::uint64_t GlyphRunList::UniqueID() const {
  return original_text_blob_ != nullptr ? original_text_blob_->UniqueID() : 0; // SK_InvalidUniqueID
}

bool GlyphRunList::AnyRunsLCD() const {
  for (const auto& r : glyph_runs_) {
    if (r.Font().GetEdging() == PlatformFont::Edging::kSubpixelAntiAlias) {
      return true;
    }
  }
  return false;
}

std::shared_ptr<const TextBlob> GlyphRunList::MakeBlob() const {
  TextBlobBuilder builder;
  for (auto& run : *this) {
    TextBlobBuilder::RunBuffer buffer;
    if (run.ScaledRotations().empty()) {
      if (run.Text().empty()) {
        buffer = builder.AllocRunPos(run.Font(), static_cast<int>(run.RunSize()), nullptr);
      } else {
        buffer = builder.AllocRunTextPos(run.Font(), static_cast<int>(run.RunSize()), static_cast<int>(run.Text().size()), nullptr);
        auto text = run.Text();
        std::memcpy(buffer.utf8text, text.data(), text.size_bytes());
        auto clusters = run.Clusters();
        std::memcpy(buffer.clusters, clusters.data(), clusters.size_bytes());
      }
      auto positions = run.Positions();
      std::memcpy(buffer.Points(), positions.data(), positions.size_bytes());
    } else {
      buffer = builder.AllocRunRSXform(run.Font(), static_cast<int>(run.RunSize()));
      RSXform* xforms = buffer.Xforms();
      for (std::size_t i = 0; i < run.RunSize(); ++i) {
        const ScalarPoint& pos = run.Positions()[i];
        const ScalarPoint& sr = run.ScaledRotations()[i];
        xforms[i] = RSXform::Make(sr.x, sr.y, pos.x, pos.y);
      }
    }
    auto glyph_ids = run.GlyphsIDs();
    std::memcpy(buffer.glyphs, glyph_ids.data(), glyph_ids.size_bytes());
  }
  return builder.Make();
}

// -- GlyphRunBuilder ----------------------------------------------------------

ScalarRect GlyphRunSourceBounds(const PlatformFont& font,
                                const PlatformPaint& paint,
                                std::span<const std::uint16_t> glyph_ids,
                                std::span<const ScalarPoint> positions,
                                std::span<const ScalarPoint> scaled_rotations) {
  const ScalarRect font_bounds = GetFontBounds(font);

  if (font_bounds.IsEmpty()) {
    // Empty font bounds are likely a font bug. TightBounds has a better
    // chance of producing useful results in this case.
    auto [strike_spec, strike_to_source_scale] = StrikeSpec::MakeCanonicalized(font, &paint);
    BulkGlyphMetrics metrics{strike_spec};
    std::span<const PlatformGlyph*> glyphs = metrics.Glyphs(glyph_ids);
    if (scaled_rotations.empty()) {
      // No RSXForm data - glyphs x/y aligned.
      auto scale_and_translate_rect = [scale = strike_to_source_scale](const ScalarRect& in, const ScalarPoint& pos) {
        return ScalarRect::MakeLTRB(in.left * scale + pos.x,
                                    in.top * scale + pos.y,
                                    in.right * scale + pos.x,
                                    in.bottom * scale + pos.y);
      };

      ScalarRect bounds;
      for (std::size_t i = 0; i < glyphs.size(); ++i) {
        if (ScalarRect r = glyphs[i]->Rect(); !r.IsEmpty()) {
          bounds.Join(scale_and_translate_rect(r, positions[i]));
        }
      }
      return bounds;
    } else {
      // RSXForm - glyphs can be any scale or rotation.
      ScalarRect bounds;
      for (std::size_t i = 0; i < glyphs.size(); ++i) {
        if (!glyphs[i]->Rect().IsEmpty()) {
          const ScalarPoint& pos = positions[i];
          const ScalarPoint& scale_rotate = scaled_rotations[i];
          // SkMatrix().setRSXform(SkRSXform{pos.x, pos.y, scaleRotate.x,
          // scaleRotate.y}): the fields are passed in this order upstream.
          ScalarMatrix xform = ScalarMatrix::MakeAll(pos.x, -pos.y, scale_rotate.x, pos.y, pos.x, scale_rotate.y);
          xform.PreScale(strike_to_source_scale, strike_to_source_scale);
          ScalarRect r = glyphs[i]->Rect();
          xform.MapRect(&r);
          bounds.Join(r);
        }
      }
      return bounds;
    }
  }

  // Use conservative bounds. All glyph have a box of fontBounds size.
  if (scaled_rotations.empty()) {
    ScalarRect bounds;
    if (!positions.empty()) {
      bounds.SetBoundsNoCheck(positions);
      if (!std::isfinite(bounds.left)) {
        bounds = ScalarRect();
      }
    }
    bounds.left += font_bounds.left;
    bounds.top += font_bounds.top;
    bounds.right += font_bounds.right;
    bounds.bottom += font_bounds.bottom;
    return bounds;
  } else {
    // RSXForm case glyphs can be any scale or rotation.
    ScalarRect bounds;
    for (std::size_t i = 0; i < positions.size(); ++i) {
      const ScalarPoint& pos = positions[i];
      const ScalarPoint& scale_rotate = scaled_rotations[i];
      // SkRSXform{pos.x(), pos.y(), scaleRotate.x(), scaleRotate.y()}, in the
      // field order of SkRSXform: scos, ssin, tx, ty.
      ScalarMatrix xform = ScalarMatrix::MakeAll(pos.x, -pos.y, scale_rotate.x, pos.y, pos.x, scale_rotate.y);
      ScalarRect r = font_bounds;
      xform.MapRect(&r);
      bounds.Join(r);
    }
    return bounds;
  }
}

GlyphRunList GlyphRunBuilder::MakeGlyphRunList(const GlyphRun& run, const PlatformPaint& paint, ScalarPoint origin) {
  const ScalarRect bounds = GlyphRunSourceBounds(run.Font(), paint, run.GlyphsIDs(), run.Positions(), run.ScaledRotations());
  return GlyphRunList{run, bounds, origin, this};
}

namespace {

std::span<const ScalarPoint> DrawTextPositions(const PlatformFont& font, std::span<const std::uint16_t> glyph_ids,
                                               ScalarPoint origin, ScalarPoint* buffer) {
  StrikeSpec strike_spec = StrikeSpec::MakeWithNoDevice(font);
  BulkGlyphMetrics storage{strike_spec};
  auto glyphs = storage.Glyphs(glyph_ids);

  ScalarPoint* position_cursor = buffer;
  ScalarPoint end_of_last_glyph = origin;
  for (const PlatformGlyph* glyph : glyphs) {
    *position_cursor++ = end_of_last_glyph;
    const ScalarPoint advance = glyph->AdvanceVector();
    end_of_last_glyph = {end_of_last_glyph.x + advance.x, end_of_last_glyph.y + advance.y};
  }
  return std::span<const ScalarPoint>(buffer, glyph_ids.size());
}

} // namespace

const GlyphRunList& GlyphRunBuilder::GlyphsToGlyphRunList(const PlatformFont& font,
                                                          const PlatformPaint& paint,
                                                          std::span<const std::uint16_t> glyph_ids,
                                                          ScalarPoint origin) {
  ScalarRect bounds;
  PrepareBuffers(static_cast<int>(glyph_ids.size()), 0);
  if (!glyph_ids.empty()) {
    std::span<const ScalarPoint> positions = DrawTextPositions(font, glyph_ids, {0, 0}, positions_.data());
    MakeGlyphRun(font, glyph_ids, positions, {}, {}, {});
    const GlyphRun& run = glyph_run_list_storage_.front();
    bounds = GlyphRunSourceBounds(run.Font(), paint, run.GlyphsIDs(), run.Positions(), run.ScaledRotations());
  }

  return SetGlyphRunList(nullptr, bounds, origin);
}

const GlyphRunList& GlyphRunBuilder::BlobToGlyphRunList(const TextBlob& blob, ScalarPoint origin) {
  // Pre-size all the buffers, so they don't move during processing.
  Initialize(blob);

  ScalarPoint* position_cursor = positions_.data();
  ScalarPoint* scaled_rotations_cursor = scaled_rotations_.data();
  for (TextBlobRunIterator it(&blob); !it.Done(); it.Next()) {
    std::size_t run_size = it.GlyphCount();
    if (run_size == 0 || !FontIsFinite(it.Font())) {
      // If no glyphs or the font is not finite, don't add the run.
      continue;
    }

    const PlatformFont& font = it.Font();
    auto glyph_ids = std::span<const std::uint16_t>{it.Glyphs(), run_size};

    std::span<const ScalarPoint> positions;
    std::span<const ScalarPoint> scaled_rotations;
    switch (it.Positioning()) {
    case TextBlobRunIterator::kDefault_Positioning: {
      positions = DrawTextPositions(font, glyph_ids, it.Offset(), position_cursor);
      position_cursor += positions.size();
      break;
    }
    case TextBlobRunIterator::kHorizontal_Positioning: {
      positions = std::span<const ScalarPoint>(position_cursor, run_size);
      for (float x : std::span<const float>{it.Pos(), glyph_ids.size()}) {
        *position_cursor++ = ScalarPoint{x, it.Offset().y};
      }
      break;
    }
    case TextBlobRunIterator::kFull_Positioning: {
      positions = std::span<const ScalarPoint>(it.Points(), run_size);
      break;
    }
    case TextBlobRunIterator::kRSXform_Positioning: {
      positions = std::span<const ScalarPoint>(position_cursor, run_size);
      scaled_rotations = std::span<const ScalarPoint>(scaled_rotations_cursor, run_size);
      for (const RSXform& xform : std::span<const RSXform>(it.Xforms(), run_size)) {
        *position_cursor++ = {xform.tx, xform.ty};
        *scaled_rotations_cursor++ = {xform.scos, xform.ssin};
      }
      break;
    }
    }

    const std::uint32_t* clusters = it.Clusters();
    MakeGlyphRun(font,
                 glyph_ids,
                 positions,
                 std::span<const char>(it.Text(), it.TextSize()),
                 std::span<const std::uint32_t>(clusters, clusters ? run_size : 0),
                 scaled_rotations);
  }

  return SetGlyphRunList(&blob, blob.Bounds(), origin);
}

void GlyphRunBuilder::Initialize(const TextBlob& blob) {
  int position_count = 0;
  int rsx_form_count = 0;
  for (TextBlobRunIterator it(&blob); !it.Done(); it.Next()) {
    if (it.Positioning() != TextBlobRunIterator::kFull_Positioning) {
      position_count += static_cast<int>(it.GlyphCount());
    }
    if (it.Positioning() == TextBlobRunIterator::kRSXform_Positioning) {
      rsx_form_count += static_cast<int>(it.GlyphCount());
    }
  }

  PrepareBuffers(position_count, rsx_form_count);
}

void GlyphRunBuilder::PrepareBuffers(int position_count, int rsx_form_count) {
  if (position_count > max_total_run_size_) {
    max_total_run_size_ = position_count;
    positions_.assign(static_cast<std::size_t>(max_total_run_size_), ScalarPoint{});
  }

  if (rsx_form_count > max_scaled_rotations_) {
    max_scaled_rotations_ = rsx_form_count;
    scaled_rotations_.assign(static_cast<std::size_t>(rsx_form_count), ScalarPoint{});
  }

  glyph_run_list_storage_.clear();
}

void GlyphRunBuilder::MakeGlyphRun(const PlatformFont& font,
                                   std::span<const std::uint16_t> glyph_ids,
                                   std::span<const ScalarPoint> positions,
                                   std::span<const char> text,
                                   std::span<const std::uint32_t> clusters,
                                   std::span<const ScalarPoint> scaled_rotations) {
  // Ignore empty runs.
  if (!glyph_ids.empty()) {
    glyph_run_list_storage_.emplace_back(font, positions, glyph_ids, text, clusters, scaled_rotations);
  }
}

const GlyphRunList& GlyphRunBuilder::SetGlyphRunList(const TextBlob* blob, const ScalarRect& bounds, ScalarPoint origin) {
  glyph_run_list_.emplace(blob, bounds, origin, std::span<const GlyphRun>(glyph_run_list_storage_), this);
  return glyph_run_list_.value();
}

} // namespace bkfont
