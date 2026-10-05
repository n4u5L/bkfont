// Ported from: skia/src/text/GlyphRun.h

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "platform/platform_font.h"
#include "rect.h"
#include "text_blob.h"

namespace bkfont {

class PlatformPaint;

// sktext::GlyphRun. Glyph ids with their positions, plus the original text
// and clusters and RSXform rotations when present.
class GlyphRun {
public:
  GlyphRun(const PlatformFont& font,
           std::span<const ScalarPoint> positions,
           std::span<const std::uint16_t> glyph_ids,
           std::span<const char> text,
           std::span<const std::uint32_t> clusters,
           std::span<const ScalarPoint> scaled_rotations);

  GlyphRun(const GlyphRun& glyph_run, const PlatformFont& font);

  std::size_t RunSize() const {
    return glyph_ids_.size();
  }
  std::span<const ScalarPoint> Positions() const {
    return positions_;
  }
  std::span<const std::uint16_t> GlyphsIDs() const {
    return glyph_ids_;
  }
  const PlatformFont& Font() const {
    return font_;
  }
  std::span<const std::uint32_t> Clusters() const {
    return clusters_;
  }
  std::span<const char> Text() const {
    return text_;
  }
  std::span<const ScalarPoint> ScaledRotations() const {
    return scaled_rotations_;
  }

private:
  // GlyphIDs and positions.
  std::span<const std::uint16_t> glyph_ids_;
  std::span<const ScalarPoint> positions_;

  // Original text from TextBlob if present. Will be empty of not present.
  std::span<const char> text_;

  // Original clusters from TextBlob if present. Will be empty if not present.
  std::span<const std::uint32_t> clusters_;

  // Possible RSXForm information
  std::span<const ScalarPoint> scaled_rotations_;

  // Font for this run modified to have glyph encoding and left alignment.
  PlatformFont font_;
};

class GlyphRunBuilder;

// sktext::GlyphRunList.
class GlyphRunList {
public:
  // Blob maybe null.
  GlyphRunList(const TextBlob* blob,
               ScalarRect bounds,
               ScalarPoint origin,
               std::span<const GlyphRun> glyph_run_list,
               GlyphRunBuilder* builder);

  GlyphRunList(const GlyphRun& glyph_run,
               const ScalarRect& bounds,
               ScalarPoint origin,
               GlyphRunBuilder* builder);

  // Zero when there is no blob.
  std::uint64_t UniqueID() const;
  bool AnyRunsLCD() const;

  bool CanCache() const {
    return original_text_blob_ != nullptr;
  }
  std::size_t RunCount() const {
    return glyph_runs_.size();
  }
  std::size_t TotalGlyphCount() const {
    std::size_t glyph_count = 0;
    for (const GlyphRun& run : *this) {
      glyph_count += run.RunSize();
    }
    return glyph_count;
  }
  std::size_t MaxGlyphRunSize() const {
    std::size_t size = 0;
    for (const GlyphRun& run : *this) {
      size = std::max(run.RunSize(), size);
    }
    return size;
  }

  bool HasRSXForm() const {
    for (const GlyphRun& run : *this) {
      if (!run.ScaledRotations().empty()) {
        return true;
      }
    }
    return false;
  }

  std::shared_ptr<const TextBlob> MakeBlob() const;

  ScalarPoint Origin() const {
    return origin_;
  }
  ScalarRect SourceBounds() const {
    return source_bounds_;
  }
  ScalarRect SourceBoundsWithOrigin() const {
    ScalarRect r = source_bounds_;
    r.Offset(origin_.x, origin_.y);
    return r;
  }
  const TextBlob* Blob() const {
    return original_text_blob_;
  }
  GlyphRunBuilder* Builder() const {
    return builder_;
  }

  auto begin() const {
    return glyph_runs_.begin();
  }
  auto end() const {
    return glyph_runs_.end();
  }
  std::size_t size() const {
    return glyph_runs_.size();
  }
  bool empty() const {
    return glyph_runs_.empty();
  }
  const GlyphRun& operator[](std::size_t i) const {
    return glyph_runs_[i];
  }

private:
  std::span<const GlyphRun> glyph_runs_;

  // The text blob is needed to hook up the call back that the TextBlob
  // destructor calls. It should be used for nothing else.
  const TextBlob* original_text_blob_{nullptr};
  const ScalarRect source_bounds_;
  const ScalarPoint origin_ = {0, 0};
  GlyphRunBuilder* const builder_;
};

// sktext::GlyphRunBuilder. Text-encoded input is not ported; runs come from
// blobs or from glyph ids.
class GlyphRunBuilder {
public:
  GlyphRunList MakeGlyphRunList(const GlyphRun& run, const PlatformPaint& paint, ScalarPoint origin);

  // Positions the glyphs with their advances from the origin, as drawing a
  // glyph-encoded text does.
  const GlyphRunList& GlyphsToGlyphRunList(const PlatformFont& font,
                                           const PlatformPaint& paint,
                                           std::span<const std::uint16_t> glyph_ids,
                                           ScalarPoint origin);

  const GlyphRunList& BlobToGlyphRunList(const TextBlob& blob, ScalarPoint origin);

  bool Empty() const {
    return glyph_run_list_storage_.empty();
  }

private:
  void Initialize(const TextBlob& blob);
  void PrepareBuffers(int position_count, int rsx_form_count);

  void MakeGlyphRun(const PlatformFont& font,
                    std::span<const std::uint16_t> glyph_ids,
                    std::span<const ScalarPoint> positions,
                    std::span<const char> text,
                    std::span<const std::uint32_t> clusters,
                    std::span<const ScalarPoint> scaled_rotations);

  const GlyphRunList& SetGlyphRunList(const TextBlob* blob, const ScalarRect& bounds, ScalarPoint origin);

  int max_total_run_size_{0};
  std::vector<ScalarPoint> positions_;
  int max_scaled_rotations_{0};
  std::vector<ScalarPoint> scaled_rotations_;

  std::vector<GlyphRun> glyph_run_list_storage_;
  std::optional<GlyphRunList> glyph_run_list_; // Defaults to no value;
};

// The source bounds of a glyph run: conservative font bounds, or the tight
// glyph bounds when the font bounds are empty.
ScalarRect GlyphRunSourceBounds(const PlatformFont& font,
                                const PlatformPaint& paint,
                                std::span<const std::uint16_t> glyph_ids,
                                std::span<const ScalarPoint> positions,
                                std::span<const ScalarPoint> scaled_rotations);

} // namespace bkfont
