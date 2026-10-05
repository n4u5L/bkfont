// Subset of blink/renderer/core/css/resolver/style_cascade.h.
//
// The cascade only decides which declaration wins for each longhand and
// records its origin and position (CascadeMap of CascadePriority). Values
// stay in the declaration blocks of the MatchResult. Apply() then applies the
// winners in StyleCascade::Apply()'s phases, independent of the order in
// which declarations were written.
//
// Not ported: animations and interpolations, custom properties and var(),
// revert/revert-layer, !important, cascade layers, CascadeFilter, explicit
// defaults on zoom change and env().
#pragma once

#include <cstdint>

#include "style/cascade_map.h"
#include "style/match_result.h"

namespace bkfont {

class StyleResolverState;

class StyleCascade {
public:
  // `match_result` must outlive the cascade and remain unchanged while it is used.
  StyleCascade(StyleResolverState& state, const MatchResult& match_result)
      : state_(state), match_result_(match_result) {}
  StyleCascade(const StyleCascade&) = delete;
  StyleCascade& operator=(const StyleCascade&) = delete;

  void Apply();

private:
  void AnalyzeIfNeeded();
  void AnalyzeMatchResult();
  void ApplyCascadeAffecting();
  void ApplyHighPriority();
  void ApplyWideOverlapping();
  void ApplyMatchResult();
  void LookupAndApply(CSSPropertyID);
  void LookupAndApplyDeclaration(CSSPropertyID, CascadePriority*);

  StyleResolverState& state_;
  const MatchResult& match_result_;
  CascadeMap map_;
  bool needs_match_result_analyze_ = true;
  // CascadeResolver::generation_. Zero means "not applied".
  uint8_t generation_ = 0;
};

} // namespace bkfont
