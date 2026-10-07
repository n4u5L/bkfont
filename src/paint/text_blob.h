// Ported from: skia/include/core/SkTextBlob.h
// Ported from: skia/include/core/SkRSXform.h
// Ported from: skia/src/core/SkTextBlobPriv.h

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "platform/platform_font.h"
#include "rect.h"

namespace bkit {

class PlatformPaint;

// SkRSXform: a compressed form of a rotation+scale matrix
// [ scos -ssin tx ]
// [ ssin  scos ty ]
// [    0     0  1 ]
struct RSXform {
  static RSXform Make(float scos, float ssin, float tx, float ty) {
    return {scos, ssin, tx, ty};
  }

  float scos;
  float ssin;
  float tx;
  float ty;
};

// SkTextBlob. Combines multiple text runs into an immutable container. Each
// text run consists of glyphs, a font, and positions. Blobs are owned by
// std::shared_ptr, which stands in for sk_sp. Serialization is not ported.
class TextBlob {
public:
  enum GlyphPositioning : std::uint8_t {
    kDefault_Positioning = 0,    // Default glyph advances -- zero scalars per glyph.
    kHorizontal_Positioning = 1, // Horizontal positioning -- one scalar per glyph.
    kFull_Positioning = 2,       // Point positioning -- two scalars per glyph.
    kRSXform_Positioning = 3,    // RSXform positioning -- four scalars per glyph.
  };

  ~TextBlob();
  TextBlob(const TextBlob&) = delete;
  TextBlob& operator=(const TextBlob&) = delete;

  // Returns conservative bounding box. Uses the font associated with each
  // glyph to determine glyph bounds, and unions all bounds. Returned bounds
  // may be larger than the bounds of all glyphs in runs.
  const ScalarRect& Bounds() const {
    return bounds_;
  }

  // Returns a non-zero value unique among all text blobs.
  std::uint32_t UniqueID() const {
    return unique_id_;
  }

  // Returns the number of intervals that intersect bounds. bounds describes
  // a pair of lines parallel to the text advance. The return count is zero or
  // a multiple of two, and is at most twice the number of glyphs in the blob.
  //
  // Pass nullptr for intervals to determine the size of the interval array.
  //
  // Runs within the blob that contain RSXform are ignored when computing
  // intercepts.
  int GetIntercepts(const float bounds[2], float intervals[], const PlatformPaint* paint = nullptr) const;

  // Returns a text blob built from a single run of glyphs positioned by
  // xpos and const_y, or null if glyphs is empty or xpos is too short.
  static std::shared_ptr<const TextBlob> MakeFromPosHGlyphs(std::span<const std::uint16_t> glyphs,
                                                            std::span<const float> xpos, float const_y,
                                                            const PlatformFont& font);
  static std::shared_ptr<const TextBlob> MakeFromPosGlyphs(std::span<const std::uint16_t> glyphs,
                                                           std::span<const ScalarPoint> pos,
                                                           const PlatformFont& font);
  static std::shared_ptr<const TextBlob> MakeFromRSXformGlyphs(std::span<const std::uint16_t> glyphs,
                                                               std::span<const RSXform> xform,
                                                               const PlatformFont& font);

  static unsigned ScalarsPerGlyph(GlyphPositioning pos);

private:
  friend class TextBlobBuilder;
  friend class TextBlobRunIterator;

  struct RunRecord {
    PlatformFont font;
    ScalarPoint offset;
    GlyphPositioning positioning = kDefault_Positioning;
    std::vector<std::uint16_t> glyphs;
    std::vector<float> pos;
    // Present only for extended runs, which carry text.
    std::vector<std::uint32_t> clusters;
    std::vector<char> text;

    std::uint32_t GlyphCount() const {
      return static_cast<std::uint32_t>(glyphs.size());
    }
    std::uint32_t TextSize() const {
      return static_cast<std::uint32_t>(text.size());
    }
    bool IsExtended() const {
      return !text.empty();
    }
  };

  TextBlob(const ScalarRect& bounds, std::vector<RunRecord> runs);

  const ScalarRect bounds_;
  const std::uint32_t unique_id_;
  const std::vector<RunRecord> runs_;
};

// SkTextBlobRunIterator. Iterate through all of the text runs of a text blob.
class TextBlobRunIterator {
public:
  explicit TextBlobRunIterator(const TextBlob* blob);

  enum GlyphPositioning : std::uint8_t {
    kDefault_Positioning = 0,
    kHorizontal_Positioning = 1,
    kFull_Positioning = 2,
    kRSXform_Positioning = 3,
  };

  bool Done() const {
    return index_ >= blob_->runs_.size();
  }
  void Next();

  std::uint32_t GlyphCount() const {
    return Run().GlyphCount();
  }
  const std::uint16_t* Glyphs() const {
    return Run().glyphs.data();
  }
  const float* Pos() const {
    return Run().pos.data();
  }
  // alias for Pos()
  const ScalarPoint* Points() const {
    return reinterpret_cast<const ScalarPoint*>(Run().pos.data());
  }
  const RSXform* Xforms() const {
    return reinterpret_cast<const RSXform*>(Run().pos.data());
  }
  const ScalarPoint& Offset() const {
    return Run().offset;
  }
  const PlatformFont& Font() const {
    return Run().font;
  }
  GlyphPositioning Positioning() const;
  unsigned ScalarsPerGlyph() const;
  const std::uint32_t* Clusters() const {
    return Run().IsExtended() ? Run().clusters.data() : nullptr;
  }
  std::uint32_t TextSize() const {
    return Run().TextSize();
  }
  const char* Text() const {
    return Run().IsExtended() ? Run().text.data() : nullptr;
  }

  bool IsLCD() const;

private:
  const TextBlob::RunRecord& Run() const {
    return blob_->runs_[index_];
  }

  const TextBlob* blob_;
  std::size_t index_ = 0;
};

// SkTextBlobBuilder. Helper class for constructing a TextBlob.
class TextBlobBuilder {
public:
  TextBlobBuilder();
  ~TextBlobBuilder();
  TextBlobBuilder(const TextBlobBuilder&) = delete;
  TextBlobBuilder& operator=(const TextBlobBuilder&) = delete;

  // Returns TextBlob built from runs of glyphs added by builder. Returned
  // TextBlob is immutable; it may be copied, but its contents may not be
  // altered. Returns nullptr if no runs of glyphs were added by builder.
  //
  // Resets TextBlobBuilder to its initial empty state, allowing it to be
  // reused to build a new set of runs.
  std::shared_ptr<const TextBlob> Make();

  // RunBuffer supplies storage for glyphs and positions within a run. The
  // pointers are valid until the next call to the builder.
  struct RunBuffer {
    std::uint16_t* glyphs;  // storage for glyph indexes in run
    float* pos;             // storage for glyph positions in run
    char* utf8text;         // storage for text UTF-8 code units in run
    std::uint32_t* clusters; // storage for glyph clusters (index of UTF-8 code unit)

    ScalarPoint* Points() const {
      return reinterpret_cast<ScalarPoint*>(pos);
    }
    RSXform* Xforms() const {
      return reinterpret_cast<RSXform*>(pos);
    }
  };

  const RunBuffer& AllocRun(const PlatformFont& font, int count, float x, float y, const ScalarRect* bounds = nullptr);
  const RunBuffer& AllocRunPosH(const PlatformFont& font, int count, float y, const ScalarRect* bounds = nullptr);
  const RunBuffer& AllocRunPos(const PlatformFont& font, int count, const ScalarRect* bounds = nullptr);
  const RunBuffer& AllocRunRSXform(const PlatformFont& font, int count);
  const RunBuffer& AllocRunText(const PlatformFont& font, int count, float x, float y, int text_byte_count,
                                const ScalarRect* bounds = nullptr);
  const RunBuffer& AllocRunTextPosH(const PlatformFont& font, int count, float y, int text_byte_count,
                                    const ScalarRect* bounds = nullptr);
  const RunBuffer& AllocRunTextPos(const PlatformFont& font, int count, int text_byte_count,
                                   const ScalarRect* bounds = nullptr);
  const RunBuffer& AllocRunTextRSXform(const PlatformFont& font, int count, int text_byte_count,
                                       const ScalarRect* bounds = nullptr);

private:
  void AllocInternal(const PlatformFont& font, TextBlob::GlyphPositioning positioning,
                     int count, int text_bytes, ScalarPoint offset, const ScalarRect* bounds);
  bool MergeRun(const PlatformFont& font, TextBlob::GlyphPositioning positioning,
                std::uint32_t count, ScalarPoint offset);
  void UpdateDeferredBounds();

  static ScalarRect ConservativeRunBounds(const TextBlob::RunRecord& run);
  static ScalarRect TightRunBounds(const TextBlob::RunRecord& run);

  std::vector<TextBlob::RunRecord> runs_;
  ScalarRect bounds_;
  bool deferred_bounds_ = false;
  RunBuffer current_run_buffer_{nullptr, nullptr, nullptr, nullptr};
};

// SkFontPriv::GetFontBounds: the typeface bounds mapped by the font's text
// matrix.
ScalarRect GetFontBounds(const PlatformFont& font);

// SkFontPriv::IsFinite.
bool FontIsFinite(const PlatformFont& font);

} // namespace bkit
