// Ported from: skia/src/core/SkStrike.cpp

#include "strike.h"

#include <utility>

#include "picture.h"
#include "strike_cache.h"

namespace bkfont {

namespace {

// use_or_generate_metrics. Strikes are never created with given metrics here.
PlatformFontMetrics UseOrGenerateMetrics(ScalerContext* context) {
  PlatformFontMetrics answer;
  context->GetFontMetrics(&answer);
  return answer;
}

} // namespace

Strike::Strike(StrikeCache* strike_cache, const StrikeSpec& strike_spec, std::unique_ptr<ScalerContext> scaler)
    : font_metrics_{UseOrGenerateMetrics(scaler.get())},
      rounding_spec_{scaler->IsSubpixel(), scaler->ComputeAxisAlignmentForHText()},
      strike_spec_{strike_spec},
      strike_cache_{strike_cache},
      scaler_context_{std::move(scaler)} {
}

class Strike::Monitor {
public:
  Monitor(Strike* strike)
      : strike_{strike} {
    strike_->Lock();
  }

  ~Monitor() {
    strike_->Unlock();
  }

private:
  Strike* const strike_;
};

void Strike::Lock() {
  strike_lock_.Acquire();
  memory_increase_ = 0;
}

void Strike::Unlock() {
  const std::size_t memory_increase = memory_increase_;
  strike_lock_.Release();
  UpdateMemoryUsage(memory_increase);
}

std::span<const PlatformGlyph*> Strike::Metrics(std::span<const std::uint16_t> glyph_ids, const PlatformGlyph* results[]) {
  Monitor m{this};
  return InternalPrepare(glyph_ids, kMetricsOnly, results);
}

std::span<const PlatformGlyph*> Strike::PreparePaths(std::span<const std::uint16_t> glyph_ids, const PlatformGlyph* results[]) {
  Monitor m{this};
  return InternalPrepare(glyph_ids, kMetricsAndPath, results);
}

std::span<const PlatformGlyph*> Strike::PrepareDrawables(std::span<const std::uint16_t> glyph_ids, const PlatformGlyph* results[]) {
  const PlatformGlyph** cursor = results;
  {
    Monitor m{this};
    for (std::uint16_t glyph_id : glyph_ids) {
      PlatformGlyph* glyph = Glyph(PackedGlyphID{glyph_id});
      PrepareForDrawable(glyph);
      *cursor++ = glyph;
    }
  }
  return {results, glyph_ids.size()};
}

void Strike::FindIntercepts(const float bounds[2], float scale, float x_pos,
                            PlatformGlyph* glyph, float* array, int* count) {
  AutoMutexExclusive lock{strike_lock_};
  glyph->EnsureIntercepts(bounds, scale, x_pos, array, count, &alloc_);
}

std::span<const PlatformGlyph*> Strike::PrepareImages(std::span<const PackedGlyphID> glyph_ids, const PlatformGlyph* results[]) {
  const PlatformGlyph** cursor = results;
  Monitor m{this};
  for (PackedGlyphID glyph_id : glyph_ids) {
    PlatformGlyph* glyph = Glyph(glyph_id);
    PrepareForImage(glyph);
    *cursor++ = glyph;
  }

  return {results, glyph_ids.size()};
}

PlatformGlyph* Strike::Glyph(GlyphDigest digest) {
  return glyph_for_index_[static_cast<std::size_t>(digest.Index())];
}

PlatformGlyph* Strike::Glyph(PackedGlyphID packed_glyph_id) {
  GlyphDigest digest = DigestFor(kDirectMask, packed_glyph_id);
  return Glyph(digest);
}

GlyphDigest Strike::DigestFor(GlyphActionType action_type, PackedGlyphID packed_glyph_id) {
  auto found = digest_for_packed_glyph_id_.find(packed_glyph_id.Value());
  GlyphDigest* digest_ptr = found != digest_for_packed_glyph_id_.end() ? &found->second : nullptr;
  if (digest_ptr != nullptr && digest_ptr->ActionFor(action_type) != GlyphAction::kUnset) {
    return *digest_ptr;
  }

  PlatformGlyph* glyph;
  if (digest_ptr != nullptr) {
    glyph = glyph_for_index_[static_cast<std::size_t>(digest_ptr->Index())];
  } else {
    glyph = alloc_.Make<PlatformGlyph>(scaler_context_->MakeGlyph(packed_glyph_id, &alloc_));
    memory_increase_ += sizeof(PlatformGlyph);
    digest_ptr = AddGlyphAndDigest(glyph);
  }

  digest_ptr->SetActionFor(action_type, glyph, this);

  return *digest_ptr;
}

GlyphDigest* Strike::AddGlyphAndDigest(PlatformGlyph* glyph) {
  std::size_t index = glyph_for_index_.size();
  GlyphDigest digest = GlyphDigest{index, *glyph};
  GlyphDigest* new_digest = &digest_for_packed_glyph_id_.insert_or_assign(glyph->GetPackedID().Value(), digest).first->second;
  glyph_for_index_.push_back(glyph);
  return new_digest;
}

bool Strike::PrepareForImage(PlatformGlyph* glyph) {
  if (glyph->SetImage(&alloc_, scaler_context_.get())) {
    memory_increase_ += glyph->ImageSize();
  }
  return glyph->Image() != nullptr;
}

bool Strike::PrepareForPath(PlatformGlyph* glyph) {
  if (glyph->SetPath(&alloc_, scaler_context_.get())) {
    memory_increase_ += glyph->Path()->ApproximateBytesUsed();
  }
  return glyph->Path() != nullptr;
}

bool Strike::PrepareForDrawable(PlatformGlyph* glyph) {
  if (glyph->SetDrawable(&alloc_, scaler_context_.get())) {
    std::size_t increase = glyph->GetDrawable()->ApproximateBytesUsed();
    memory_increase_ += increase;
  }
  return glyph->GetDrawable() != nullptr;
}

std::span<const PlatformGlyph*> Strike::InternalPrepare(std::span<const std::uint16_t> glyph_ids, PathDetail path_detail, const PlatformGlyph** results) {
  const PlatformGlyph** cursor = results;
  for (std::uint16_t glyph_id : glyph_ids) {
    PlatformGlyph* glyph = Glyph(PackedGlyphID{glyph_id});
    if (path_detail == kMetricsAndPath) {
      PrepareForPath(glyph);
    }
    *cursor++ = glyph;
  }

  return {results, glyph_ids.size()};
}

void Strike::UpdateMemoryUsage(std::size_t increase) {
  if (increase > 0) {
    // removed_ and the cache's total memory are managed under the cache's
    // lock. This allows them to be accessed under LRU operation.
    AutoMutexExclusive lock{strike_cache_->lock_};
    memory_used_ += increase;
    if (!removed_) {
      strike_cache_->total_memory_used_ += increase;
    }
  }
}

} // namespace bkfont
