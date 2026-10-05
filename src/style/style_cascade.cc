// Adapted from core/css/resolver/style_cascade.cc and cascade_expansion.
#include "style_cascade.h"

#include <cassert>
#include <limits>

#include "style/style_builder.h"
#include "style/style_resolver_state.h"

namespace bkfont {
namespace {

// CascadeExpansion objects which exceed these limits emit nothing.
constexpr wtf_size_t kMaxDeclarationIndex = std::numeric_limits<uint16_t>::max();
constexpr wtf_size_t kMaxMatchedPropertiesIndex = std::numeric_limits<uint16_t>::max();

const CSSValue* ValueAt(const MatchResult& result, uint32_t position) {
  const auto& properties = result.GetMatchedProperties()[DecodeMatchedPropertiesIndex(position)].properties;
  return properties->Entries()[DecodeDeclarationIndex(position)].value.get();
}

} // namespace

void StyleCascade::Apply() {
  AnalyzeIfNeeded();
  state_.UpdateLengthConversionData();

  ++generation_;
  assert(generation_ <= CascadePriority::kGenerationMask);

  ApplyCascadeAffecting();

  ApplyHighPriority();
  state_.UpdateFont();

  if (map_.Has(CSSPropertyID::kLineHeight)) LookupAndApply(CSSPropertyID::kLineHeight);
  state_.UpdateLineHeight();

  ApplyWideOverlapping();

  ApplyMatchResult();
  // ApplyInterpolations() and ApplyUnresolvedEnv(): not ported.
}

void StyleCascade::AnalyzeIfNeeded() {
  if (!needs_match_result_analyze_) return;
  AnalyzeMatchResult();
  needs_match_result_analyze_ = false;
}

void StyleCascade::AnalyzeMatchResult() {
  // AddExplicitDefaults(): only needed when zoom changes during the cascade.
  // No cascade-affecting property is ported, so the zoom is fixed.
  const auto& matched = match_result_.GetMatchedProperties();
  for (wtf_size_t index = 0; index < matched.size(); ++index) {
    const auto entries = matched[index].properties->Entries();
    if (index > kMaxMatchedPropertiesIndex || entries.size() > kMaxDeclarationIndex + 1) continue;
    // ExpandCascade(): declarations are stored as longhands. Surrogates
    // (-webkit-writing-mode, -webkit-text-orientation, line-break) cascade as
    // the property they stand for (ResolveSurrogate()); the ported surrogates
    // do not depend on the writing direction.
    for (wtf_size_t property_idx = 0; property_idx < entries.size(); ++property_idx) {
      const CSSPropertyID id = entries[property_idx].property;
      const CSSPropertyID surrogate_for = GetCSSPropertyMetadata(id)->surrogate_for;
      map_.Add(surrogate_for != CSSPropertyID::kInvalid ? surrogate_for : id,
               CascadePriority(matched[index].origin,
                               EncodeMatchResultPosition(static_cast<uint16_t>(index),
                                                         static_cast<uint16_t>(property_idx))));
    }
  }
}

void StyleCascade::ApplyCascadeAffecting() {
  // direction and writing-mode are applied first. Upstream reanalyzes the
  // match result when either changes and a surrogate depends on them; the
  // ported surrogates map to fixed properties, so the analysis stays valid.
  // zoom is not ported.
  if (map_.Has(CSSPropertyID::kDirection)) LookupAndApply(CSSPropertyID::kDirection);
  if (map_.Has(CSSPropertyID::kWritingMode)) LookupAndApply(CSSPropertyID::kWritingMode);
}

void StyleCascade::ApplyHighPriority() {
  for (auto id = kFirstCSSProperty; id <= kLastHighPriorityCSSProperty;
       id = static_cast<CSSPropertyID>(static_cast<int>(id) + 1)) {
    if (map_.Has(id)) LookupAndApply(id);
  }
}

void StyleCascade::ApplyWideOverlapping() {
  // -webkit-border-image, perspective-origin, transform-origin and
  // vertical-align are applied here before the longhands they overlap. None
  // of them is ported.
}

void StyleCascade::ApplyMatchResult() {
  // All the high-priority properties were dealt with in ApplyHighPriority().
  for (auto id = static_cast<CSSPropertyID>(static_cast<int>(kLastHighPriorityCSSProperty) + 1);
       id <= kLastLonghandCSSProperty; id = static_cast<CSSPropertyID>(static_cast<int>(id) + 1)) {
    CascadePriority* priority = map_.Find(id);
    // Already applied this generation (e.g. line-height).
    if (!priority || priority->GetGeneration() >= generation_) continue;
    LookupAndApplyDeclaration(id, priority);
  }
}

void StyleCascade::LookupAndApply(CSSPropertyID id) {
  CascadePriority* priority = map_.Find(id);
  if (!priority) return;
  // LookupAndApplyValue(): every local origin is below kAnimation.
  LookupAndApplyDeclaration(id, priority);
}

void StyleCascade::LookupAndApplyDeclaration(CSSPropertyID id, CascadePriority* priority) {
  if (priority->GetGeneration() >= generation_) return;
  *priority = CascadePriority(*priority, generation_);
  assert(priority->HasOrigin());
  StyleBuilder::ApplyPhysicalProperty(id, state_, *ValueAt(match_result_, priority->GetPosition()));
}

} // namespace bkfont
