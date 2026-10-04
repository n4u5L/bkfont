// Source: Blink's generated runtime_enabled_features_test_helpers.h.
// Save and restore each existing runtime feature around an official test.
#pragma once
#include "runtime_enabled_features.h"
namespace bkfont {
template <bool (*Getter)(), void (*Setter)(bool)>
class ScopedRuntimeFeatureForTest {
public:
  explicit ScopedRuntimeFeatureForTest(bool value)
      : previous_(Getter()) {
    Setter(value);
  }
  ~ScopedRuntimeFeatureForTest() {
    Setter(previous_);
  }
  ScopedRuntimeFeatureForTest(const ScopedRuntimeFeatureForTest&) = delete;
  ScopedRuntimeFeatureForTest& operator=(const ScopedRuntimeFeatureForTest&) = delete;

private:
  bool previous_;
};
using ScopedShapeResultCachedPreviousSafeToBreakOffsetForTest = ScopedRuntimeFeatureForTest<&RuntimeEnabledFeatures::ShapeResultCachedPreviousSafeToBreakOffsetEnabled, &RuntimeEnabledFeatures::SetShapeResultCachedPreviousSafeToBreakOffsetEnabled>;
using ScopedCSSChUnitSpecCompliantFallbackForTest = ScopedRuntimeFeatureForTest<&RuntimeEnabledFeatures::CSSChUnitSpecCompliantFallbackEnabled, &RuntimeEnabledFeatures::SetCSSChUnitSpecCompliantFallbackEnabled>;
using ScopedCSSHexAlphaColorForTest = ScopedRuntimeFeatureForTest<&RuntimeEnabledFeatures::CSSHexAlphaColorEnabled, &RuntimeEnabledFeatures::SetCSSHexAlphaColorEnabled>;
using ScopedFontMatchAliasesAsLastResortForTest = ScopedRuntimeFeatureForTest<&RuntimeEnabledFeatures::FontMatchAliasesAsLastResortEnabled, &RuntimeEnabledFeatures::SetFontMatchAliasesAsLastResortEnabled>;
using ScopedFontPresentWinForTest = ScopedRuntimeFeatureForTest<&RuntimeEnabledFeatures::FontPresentWinEnabled, &RuntimeEnabledFeatures::SetFontPresentWinEnabled>;
using ScopedFontSrcLocalMatchingForTest = ScopedRuntimeFeatureForTest<&RuntimeEnabledFeatures::FontSrcLocalMatchingEnabled, &RuntimeEnabledFeatures::SetFontSrcLocalMatchingEnabled>;
using ScopedFontSystemFallbackNotoCjkForTest = ScopedRuntimeFeatureForTest<&RuntimeEnabledFeatures::FontSystemFallbackNotoCjkEnabled, &RuntimeEnabledFeatures::SetFontSystemFallbackNotoCjkEnabled>;
using ScopedIgnoreLetterSpacingInCursiveScriptsForTest = ScopedRuntimeFeatureForTest<&RuntimeEnabledFeatures::IgnoreLetterSpacingInCursiveScriptsEnabled, &RuntimeEnabledFeatures::SetIgnoreLetterSpacingInCursiveScriptsEnabled>;
using ScopedLayoutNGShapeCacheForTest = ScopedRuntimeFeatureForTest<&RuntimeEnabledFeatures::LayoutNGShapeCacheEnabled, &RuntimeEnabledFeatures::SetLayoutNGShapeCacheEnabled>;
using ScopedMathMLOperatorRTLMirroringForTest = ScopedRuntimeFeatureForTest<&RuntimeEnabledFeatures::MathMLOperatorRTLMirroringEnabled, &RuntimeEnabledFeatures::SetMathMLOperatorRTLMirroringEnabled>;
using ScopedNoFontAntialiasingForTest = ScopedRuntimeFeatureForTest<&RuntimeEnabledFeatures::NoFontAntialiasingEnabled, &RuntimeEnabledFeatures::SetNoFontAntialiasingEnabled>;
using ScopedScriptRunIteratorCombiningMarkAlwaysForTest = ScopedRuntimeFeatureForTest<&RuntimeEnabledFeatures::ScriptRunIteratorCombiningMarkAlwaysEnabled, &RuntimeEnabledFeatures::SetScriptRunIteratorCombiningMarkAlwaysEnabled>;
using ScopedScriptRunIteratorCombiningMarksForTest = ScopedRuntimeFeatureForTest<&RuntimeEnabledFeatures::ScriptRunIteratorCombiningMarksEnabled, &RuntimeEnabledFeatures::SetScriptRunIteratorCombiningMarksEnabled>;
using ScopedSystemFallbackEmojiVSSupportForTest = ScopedRuntimeFeatureForTest<&RuntimeEnabledFeatures::SystemFallbackEmojiVSSupportEnabled, &RuntimeEnabledFeatures::SetSystemFallbackEmojiVSSupportEnabled>;
using ScopedTabSizeWithSpacingForTest = ScopedRuntimeFeatureForTest<&RuntimeEnabledFeatures::TabSizeWithSpacingEnabled, &RuntimeEnabledFeatures::SetTabSizeWithSpacingEnabled>;
using ScopedTabWidthNegativePositionForTest = ScopedRuntimeFeatureForTest<&RuntimeEnabledFeatures::TabWidthNegativePositionEnabled, &RuntimeEnabledFeatures::SetTabWidthNegativePositionEnabled>;
using ScopedTextSpacingTrimFallbackForTest = ScopedRuntimeFeatureForTest<&RuntimeEnabledFeatures::TextSpacingTrimFallbackEnabled, &RuntimeEnabledFeatures::SetTextSpacingTrimFallbackEnabled>;
using ScopedTextSpacingTrimFallback2ForTest = ScopedRuntimeFeatureForTest<&RuntimeEnabledFeatures::TextSpacingTrimFallback2Enabled, &RuntimeEnabledFeatures::SetTextSpacingTrimFallback2Enabled>;
} // namespace bkfont
