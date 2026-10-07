// Subset of core/css/resolver/style_resolver.h. Document inputs come from
// StyleHostContext; there is no DOM, tree scope, GC or ambient device scale.
#pragma once

#include <memory>

#include "style/computed_style.h"
#include "style/inline_style.h"
#include "style/match_result.h"
#include "style/style_host_context.h"

namespace bkit {

// The element facts StyleAdjuster::AdjustComputedStyle() reads.
struct StyleAdjustInput {
  // An atomic inline-level box (IsDisplayReplacedType()): text decorations
  // do not propagate into it.
  bool is_atomic_inline = false;
  // An inline box or text within the inline formatting context. Upstream
  // turns an inline with a different writing mode into an inline-block; this
  // model has no nested formatting contexts, so it keeps the parent's
  // writing mode and text orientation instead.
  bool is_inline_content = false;
};

class StyleResolver {
public:
  // From the settings, font selector and zoom factors of `host`. Hosts use
  // StyleHostContext::InitialStyle(), which caches this.
  static std::shared_ptr<const ComputedStyle> CreateInitialStyle(const StyleHostContext& host);
  // `parent_style` is null for the root. The node starts from the initial
  // value environment (inherited fields from the parent, the others from the
  // initial style), then StyleCascade applies the winning declarations of
  // `match_result`. `legacy_style` is applied before the cascade, below every
  // declaration.
  static std::shared_ptr<const ComputedStyle> Resolve(const StyleHostContext& host, const ComputedStyle* parent_style,
                                                      const MatchResult& match_result,
                                                      const InlineStyle* legacy_style = nullptr,
                                                      StyleAdjustInput adjust = {});
};

} // namespace bkit
