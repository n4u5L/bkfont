// Ported from: skia/src/core/SkTextBlob.cpp
// Ported from: skia/src/core/SkFont.cpp

#include "text_blob.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <utility>

#include "glyph_run.h"
#include "matrix.h"
#include "platform_paint.h"
#include "strike_spec.h"

namespace bkfont {

namespace {

// SkFontPriv::kCanonicalTextSizeForPaths.
constexpr int kCanonicalTextSizeForPaths = 64;

std::uint32_t NextID() {
  static std::atomic<std::uint32_t> next_id{1};
  std::uint32_t id;
  do {
    id = next_id.fetch_add(1, std::memory_order_relaxed);
  } while (id == 0); // SK_InvalidGenID
  return id;
}

ScalarMatrix MatrixFromRSXform(const RSXform& xform) {
  // SkMatrix::setRSXform.
  return ScalarMatrix::MakeAll(xform.scos, -xform.ssin, xform.tx, xform.ssin, xform.scos, xform.ty);
}

ScalarRect MapQuadToRect(const RSXform& xform, const ScalarRect& rect) {
  ScalarRect mapped = rect;
  MatrixFromRSXform(xform).MapRect(&mapped);
  return mapped;
}

ScalarRect MakeOffset(const ScalarRect& r, float dx, float dy) {
  ScalarRect result = r;
  result.Offset(dx, dy);
  return result;
}

// SkRect::BoundsOrEmpty.
ScalarRect BoundsOrEmpty(std::span<const ScalarPoint> points) {
  if (points.empty()) {
    return ScalarRect();
  }
  ScalarRect bounds;
  bounds.SetBoundsNoCheck(points);
  if (!std::isfinite(bounds.left)) {
    return ScalarRect();
  }
  return bounds;
}

int GetGlyphRunIntercepts(const GlyphRun& glyph_run,
                          const PlatformPaint& paint,
                          const float bounds[2],
                          float intervals[],
                          int* interval_count) {
  float scale = 1;
  PlatformPaint intercept_paint{paint};
  PlatformFont intercept_font{glyph_run.Font()};

  // The paint has no path effect, so the canonical size can be used.
  // If the wrong size is going to be used, don't hint anything.
  intercept_font.SetHinting(FontHinting::kNone);
  intercept_font.SetSubpixel(true);
  scale = intercept_font.GetSize() / kCanonicalTextSizeForPaths;
  intercept_font.SetSize(static_cast<float>(kCanonicalTextSizeForPaths));
  // Note: scale can be zero here (even if it wasn't before the divide). It
  // can also be very very small. Downstream we will check for the resulting
  // coordinates being non-finite anyway. The paint is always a fill, so
  // there is no stroke width to adjust.

  StrikeSpec strike_spec = StrikeSpec::MakeWithNoDevice(intercept_font, &intercept_paint);
  BulkGlyphMetricsAndPaths metrics_and_paths{strike_spec};

  const ScalarPoint* pos_cursor = glyph_run.Positions().data();
  for (const PlatformGlyph* glyph : metrics_and_paths.Glyphs(glyph_run.GlyphsIDs())) {
    ScalarPoint pos = *pos_cursor++;
    if (glyph->Path() != nullptr) {
      // The typeface is scaled, so un-scale the bounds to be in the space of
      // the typeface. Also ensure the bounds are properly offset by the
      // vertical positioning of the glyph.
      float scaled_bounds[2] = {
          (bounds[0] - pos.y) / scale,
          (bounds[1] - pos.y) / scale};
      metrics_and_paths.FindIntercepts(scaled_bounds, scale, pos.x, glyph, intervals, interval_count);
    }
  }
  return *interval_count;
}

} // namespace

ScalarRect GetFontBounds(const PlatformFont& font) {
  ScalarMatrix m;
  m.SetScale(font.GetSize() * font.GetScaleX(), font.GetSize());
  m.PostSkew(font.GetSkewX(), 0);

  ScalarRect bounds = font.GetTypeface()->GetBounds();
  m.MapRect(&bounds);
  return bounds;
}

bool FontIsFinite(const PlatformFont& font) {
  return std::isfinite(font.GetSize()) && std::isfinite(font.GetScaleX()) && std::isfinite(font.GetSkewX());
}

// -- TextBlob -----------------------------------------------------------------

TextBlob::TextBlob(const ScalarRect& bounds, std::vector<RunRecord> runs)
    : bounds_(bounds), unique_id_(NextID()), runs_(std::move(runs)) {
}

TextBlob::~TextBlob() = default;

unsigned TextBlob::ScalarsPerGlyph(GlyphPositioning pos) {
  const std::uint8_t kScalarsPerPositioning[] = {
      0, // kDefault_Positioning
      1, // kHorizontal_Positioning
      2, // kFull_Positioning
      4, // kRSXform_Positioning
  };
  return kScalarsPerPositioning[pos];
}

int TextBlob::GetIntercepts(const float bounds[2], float intervals[], const PlatformPaint* paint) const {
  PlatformPaint default_paint;
  if (paint == nullptr) {
    paint = &default_paint;
  }

  GlyphRunBuilder builder;
  const GlyphRunList& glyph_run_list = builder.BlobToGlyphRunList(*this, {0, 0});

  int interval_count = 0;
  for (const GlyphRun& glyph_run : glyph_run_list) {
    // Ignore RSXForm runs.
    if (glyph_run.ScaledRotations().empty()) {
      interval_count = GetGlyphRunIntercepts(glyph_run, *paint, bounds, intervals, &interval_count);
    }
  }

  return interval_count;
}

std::shared_ptr<const TextBlob> TextBlob::MakeFromPosHGlyphs(std::span<const std::uint16_t> glyphs,
                                                             std::span<const float> xpos, float const_y,
                                                             const PlatformFont& font) {
  const std::size_t count = glyphs.size();
  if (count == 0 || xpos.size() < count) {
    return nullptr;
  }

  TextBlobBuilder builder;
  auto buffer = builder.AllocRunPosH(font, static_cast<int>(count), const_y);
  std::copy_n(glyphs.data(), count, buffer.glyphs);
  std::memcpy(buffer.pos, xpos.data(), count * sizeof(float));
  return builder.Make();
}

std::shared_ptr<const TextBlob> TextBlob::MakeFromPosGlyphs(std::span<const std::uint16_t> glyphs,
                                                            std::span<const ScalarPoint> pos,
                                                            const PlatformFont& font) {
  const std::size_t count = glyphs.size();
  if (count == 0 || pos.size() < count) {
    return nullptr;
  }

  TextBlobBuilder builder;
  auto buffer = builder.AllocRunPos(font, static_cast<int>(count));
  std::copy_n(glyphs.data(), count, buffer.glyphs);
  std::memcpy(buffer.Points(), pos.data(), count * sizeof(ScalarPoint));
  return builder.Make();
}

std::shared_ptr<const TextBlob> TextBlob::MakeFromRSXformGlyphs(std::span<const std::uint16_t> glyphs,
                                                                std::span<const RSXform> xform,
                                                                const PlatformFont& font) {
  const std::size_t count = glyphs.size();
  if (count == 0 || xform.size() < count) {
    return nullptr;
  }

  TextBlobBuilder builder;
  auto buffer = builder.AllocRunRSXform(font, static_cast<int>(count));
  std::copy_n(glyphs.data(), count, buffer.glyphs);
  std::memcpy(buffer.Xforms(), xform.data(), count * sizeof(RSXform));
  return builder.Make();
}

// -- TextBlobRunIterator ------------------------------------------------------

TextBlobRunIterator::TextBlobRunIterator(const TextBlob* blob)
    : blob_(blob) {
}

void TextBlobRunIterator::Next() {
  if (!Done()) {
    ++index_;
  }
}

TextBlobRunIterator::GlyphPositioning TextBlobRunIterator::Positioning() const {
  return static_cast<GlyphPositioning>(Run().positioning);
}

unsigned TextBlobRunIterator::ScalarsPerGlyph() const {
  return TextBlob::ScalarsPerGlyph(Run().positioning);
}

bool TextBlobRunIterator::IsLCD() const {
  return Run().font.GetEdging() == PlatformFont::Edging::kSubpixelAntiAlias;
}

// -- TextBlobBuilder ----------------------------------------------------------

TextBlobBuilder::TextBlobBuilder() = default;

TextBlobBuilder::~TextBlobBuilder() = default;

ScalarRect TextBlobBuilder::TightRunBounds(const TextBlob::RunRecord& run) {
  const PlatformFont& font = run.font;
  ScalarRect bounds;

  if (TextBlob::kDefault_Positioning == run.positioning) {
    font.MeasureText(run.glyphs, &bounds);
    return MakeOffset(bounds, run.offset.x, run.offset.y);
  }

  std::vector<ScalarRect> glyph_bounds(run.GlyphCount());
  font.GetBounds(run.glyphs, glyph_bounds);

  if (TextBlob::kRSXform_Positioning == run.positioning) {
    bounds = ScalarRect();
    const RSXform* xform = reinterpret_cast<const RSXform*>(run.pos.data());
    for (unsigned i = 0; i < run.GlyphCount(); ++i) {
      bounds.Join(MapQuadToRect(xform[i], glyph_bounds[i]));
    }
  } else {
    // kFull_Positioning       => [ x, y, x, y... ]
    // kHorizontal_Positioning => [ x, x, x... ]
    //                            (const y applied by runBounds.offset(run->offset()) later)
    const float horizontal_const_y = 0;
    const float* glyph_pos_x = run.pos.data();
    const float* glyph_pos_y = (run.positioning == TextBlob::kFull_Positioning) ? glyph_pos_x + 1 : &horizontal_const_y;
    const unsigned pos_x_inc = TextBlob::ScalarsPerGlyph(run.positioning);
    const unsigned pos_y_inc = (run.positioning == TextBlob::kFull_Positioning) ? pos_x_inc : 0;

    bounds = ScalarRect();
    for (unsigned i = 0; i < run.GlyphCount(); ++i) {
      bounds.Join(MakeOffset(glyph_bounds[i], *glyph_pos_x, *glyph_pos_y));
      glyph_pos_x += pos_x_inc;
      glyph_pos_y += pos_y_inc;
    }
  }

  return MakeOffset(bounds, run.offset.x, run.offset.y);
}

ScalarRect TextBlobBuilder::ConservativeRunBounds(const TextBlob::RunRecord& run) {
  const ScalarRect font_bounds = GetFontBounds(run.font);
  if (font_bounds.IsEmpty()) {
    // Empty font bounds are likely a font bug. TightBounds has a better
    // chance of producing useful results in this case.
    return TightRunBounds(run);
  }

  // Compute the glyph position bbox.
  ScalarRect bounds;
  switch (run.positioning) {
  case TextBlob::kHorizontal_Positioning: {
    const float* glyph_pos = run.pos.data();
    float min_x = *glyph_pos;
    float max_x = *glyph_pos;
    for (unsigned i = 1; i < run.GlyphCount(); ++i) {
      float x = glyph_pos[i];
      min_x = std::min(x, min_x);
      max_x = std::max(x, max_x);
    }

    bounds = ScalarRect::MakeLTRB(min_x, 0, max_x, 0);
  } break;
  case TextBlob::kFull_Positioning: {
    const ScalarPoint* glyph_pos_pts = reinterpret_cast<const ScalarPoint*>(run.pos.data());
    bounds = BoundsOrEmpty({glyph_pos_pts, run.GlyphCount()});
  } break;
  case TextBlob::kRSXform_Positioning: {
    const RSXform* xform = reinterpret_cast<const RSXform*>(run.pos.data());
    bounds = ScalarRect();
    for (unsigned i = 0; i < run.GlyphCount(); ++i) {
      bounds.Join(MapQuadToRect(xform[i], font_bounds));
    }
  } break;
  default:
    // unsupported positioning mode
    break;
  }

  if (run.positioning != TextBlob::kRSXform_Positioning) {
    // Expand by typeface glyph bounds.
    bounds.left += font_bounds.left;
    bounds.top += font_bounds.top;
    bounds.right += font_bounds.right;
    bounds.bottom += font_bounds.bottom;
  }

  // Offset by run position.
  return MakeOffset(bounds, run.offset.x, run.offset.y);
}

void TextBlobBuilder::UpdateDeferredBounds() {
  if (!deferred_bounds_) {
    return;
  }

  const TextBlob::RunRecord& run = runs_.back();
  // FIXME: we should also use conservative bounds for kDefault_Positioning.
  ScalarRect run_bounds = TextBlob::kDefault_Positioning == run.positioning ? TightRunBounds(run) : ConservativeRunBounds(run);
  bounds_.Join(run_bounds);
  deferred_bounds_ = false;
}

bool TextBlobBuilder::MergeRun(const PlatformFont& font, TextBlob::GlyphPositioning positioning,
                               std::uint32_t count, ScalarPoint offset) {
  if (runs_.empty()) {
    return false;
  }

  TextBlob::RunRecord& run = runs_.back();

  if (run.TextSize() != 0) {
    return false;
  }

  if (run.positioning != positioning || !(run.font == font) || (run.GlyphCount() + count < run.GlyphCount())) {
    return false;
  }

  // we can merge same-font/same-positioning runs in the following cases:
  //   * fully positioned run following another fully positioned run
  //   * horizontally postioned run following another horizontally
  //     positioned run with the same y-offset
  if (TextBlob::kFull_Positioning != positioning &&
      (TextBlob::kHorizontal_Positioning != positioning || run.offset.y != offset.y)) {
    return false;
  }

  const std::uint32_t pre_merge_count = run.GlyphCount();
  const unsigned scalars = TextBlob::ScalarsPerGlyph(positioning);
  run.glyphs.resize(static_cast<std::size_t>(pre_merge_count) + count);
  run.pos.resize((static_cast<std::size_t>(pre_merge_count) + count) * scalars);

  // Callers expect the buffers to point at the newly added slice, ant not at
  // the beginning.
  current_run_buffer_.glyphs = run.glyphs.data() + pre_merge_count;
  current_run_buffer_.pos = run.pos.data() + static_cast<std::size_t>(pre_merge_count) * scalars;

  return true;
}

void TextBlobBuilder::AllocInternal(const PlatformFont& font,
                                    TextBlob::GlyphPositioning positioning,
                                    int count, int text_size, ScalarPoint offset,
                                    const ScalarRect* bounds) {
  if (count <= 0 || text_size < 0) {
    current_run_buffer_ = {nullptr, nullptr, nullptr, nullptr};
    return;
  }

  if (text_size != 0 || !MergeRun(font, positioning, static_cast<std::uint32_t>(count), offset)) {
    UpdateDeferredBounds();

    TextBlob::RunRecord run;
    run.font = font;
    run.offset = offset;
    run.positioning = positioning;
    run.glyphs.resize(static_cast<std::size_t>(count));
    run.pos.resize(static_cast<std::size_t>(count) * TextBlob::ScalarsPerGlyph(positioning));
    if (text_size) {
      run.clusters.resize(static_cast<std::size_t>(count));
      run.text.resize(static_cast<std::size_t>(text_size));
    }
    runs_.push_back(std::move(run));

    TextBlob::RunRecord& stored = runs_.back();
    current_run_buffer_.glyphs = stored.glyphs.data();
    current_run_buffer_.pos = stored.pos.data();
    current_run_buffer_.utf8text = text_size ? stored.text.data() : nullptr;
    current_run_buffer_.clusters = text_size ? stored.clusters.data() : nullptr;
  }

  if (!deferred_bounds_) {
    if (bounds) {
      bounds_.Join(*bounds);
    } else {
      deferred_bounds_ = true;
    }
  }
}

const TextBlobBuilder::RunBuffer& TextBlobBuilder::AllocRun(const PlatformFont& font, int count, float x, float y,
                                                            const ScalarRect* bounds) {
  AllocInternal(font, TextBlob::kDefault_Positioning, count, 0, {x, y}, bounds);
  return current_run_buffer_;
}

const TextBlobBuilder::RunBuffer& TextBlobBuilder::AllocRunPosH(const PlatformFont& font, int count, float y,
                                                                const ScalarRect* bounds) {
  AllocInternal(font, TextBlob::kHorizontal_Positioning, count, 0, {0, y}, bounds);
  return current_run_buffer_;
}

const TextBlobBuilder::RunBuffer& TextBlobBuilder::AllocRunPos(const PlatformFont& font, int count,
                                                               const ScalarRect* bounds) {
  AllocInternal(font, TextBlob::kFull_Positioning, count, 0, {0, 0}, bounds);
  return current_run_buffer_;
}

const TextBlobBuilder::RunBuffer& TextBlobBuilder::AllocRunRSXform(const PlatformFont& font, int count) {
  AllocInternal(font, TextBlob::kRSXform_Positioning, count, 0, {0, 0}, nullptr);
  return current_run_buffer_;
}

const TextBlobBuilder::RunBuffer& TextBlobBuilder::AllocRunText(const PlatformFont& font, int count, float x, float y,
                                                                int text_byte_count, const ScalarRect* bounds) {
  AllocInternal(font, TextBlob::kDefault_Positioning, count, text_byte_count, {x, y}, bounds);
  return current_run_buffer_;
}

const TextBlobBuilder::RunBuffer& TextBlobBuilder::AllocRunTextPosH(const PlatformFont& font, int count, float y,
                                                                    int text_byte_count, const ScalarRect* bounds) {
  AllocInternal(font, TextBlob::kHorizontal_Positioning, count, text_byte_count, {0, y}, bounds);
  return current_run_buffer_;
}

const TextBlobBuilder::RunBuffer& TextBlobBuilder::AllocRunTextPos(const PlatformFont& font, int count,
                                                                   int text_byte_count, const ScalarRect* bounds) {
  AllocInternal(font, TextBlob::kFull_Positioning, count, text_byte_count, {0, 0}, bounds);
  return current_run_buffer_;
}

const TextBlobBuilder::RunBuffer& TextBlobBuilder::AllocRunTextRSXform(const PlatformFont& font, int count,
                                                                       int text_byte_count, const ScalarRect* bounds) {
  AllocInternal(font, TextBlob::kRSXform_Positioning, count, text_byte_count, {0, 0}, bounds);
  return current_run_buffer_;
}

std::shared_ptr<const TextBlob> TextBlobBuilder::Make() {
  if (runs_.empty()) {
    // We don't instantiate empty blobs.
    return nullptr;
  }

  UpdateDeferredBounds();

  std::shared_ptr<const TextBlob> blob(new TextBlob(bounds_, std::move(runs_)));

  runs_.clear();
  bounds_ = ScalarRect();
  deferred_bounds_ = false;
  current_run_buffer_ = {nullptr, nullptr, nullptr, nullptr};

  return blob;
}

} // namespace bkfont
