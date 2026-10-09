// Ported from: blink/renderer/build/scripts/templates/runtime_enabled_features.h.tmpl
// Ported from: blink/renderer/platform/runtime_enabled_features.json5
// Ported from: chromium/content/common/features.cc
// Ported from: chromium/content/child/runtime_features.cc
// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
// Defaults: Blink platform/runtime_enabled_features.json5 (Windows), except
// FontSrcLocalMatching: content/common/features.cc, forwarded by runtime_features.cc.
// Browser field trials are replaced by explicit configuration before font use.
#pragma once
namespace bkit {

class RuntimeEnabledFeatures {
public:
  static bool BidiCaretAffinityEnabled() {
    return BidiCaretAffinity_;
  }
  static void SetBidiCaretAffinityEnabled(bool value) {
    BidiCaretAffinity_ = value;
  }
  static bool CanvasTextCacheLimitEnabled() {
    return CanvasTextCacheLimit_;
  }
  static void SetCanvasTextCacheLimitEnabled(bool value) {
    CanvasTextCacheLimit_ = value;
  }
  static bool ShapeResultCachedPreviousSafeToBreakOffsetEnabled() {
    return ShapeResultCachedPreviousSafeToBreakOffset_;
  }
  static void SetShapeResultCachedPreviousSafeToBreakOffsetEnabled(bool value) {
    ShapeResultCachedPreviousSafeToBreakOffset_ = value;
  }
  static bool CSSChUnitSpecCompliantFallbackEnabled() {
    return CSSChUnitSpecCompliantFallback_;
  }
  static void SetCSSChUnitSpecCompliantFallbackEnabled(bool value) {
    CSSChUnitSpecCompliantFallback_ = value;
  }
  static bool CSSHexAlphaColorEnabled() {
    return CSSHexAlphaColor_;
  }
  static void SetCSSHexAlphaColorEnabled(bool value) {
    CSSHexAlphaColor_ = value;
  }
  static bool FontMatchAliasesAsLastResortEnabled() {
    return FontMatchAliasesAsLastResort_;
  }
  static void SetFontMatchAliasesAsLastResortEnabled(bool value) {
    FontMatchAliasesAsLastResort_ = value;
  }
  static bool FontPresentWinEnabled() {
    return FontPresentWin_;
  }
  static void SetFontPresentWinEnabled(bool value) {
    FontPresentWin_ = value;
  }
  static bool FontSrcLocalMatchingEnabled() {
    return FontSrcLocalMatching_;
  }
  static void SetFontSrcLocalMatchingEnabled(bool value) {
    FontSrcLocalMatching_ = value;
  }
  static bool FontSystemFallbackNotoCjkEnabled() {
    return FontSystemFallbackNotoCjk_;
  }
  static void SetFontSystemFallbackNotoCjkEnabled(bool value) {
    FontSystemFallbackNotoCjk_ = value;
  }
  static bool IgnoreLetterSpacingInCursiveScriptsEnabled() {
    return IgnoreLetterSpacingInCursiveScripts_;
  }
  static void SetIgnoreLetterSpacingInCursiveScriptsEnabled(bool value) {
    IgnoreLetterSpacingInCursiveScripts_ = value;
  }
  static bool LayoutNGShapeCacheEnabled() {
    return LayoutNGShapeCache_;
  }
  static void SetLayoutNGShapeCacheEnabled(bool value) {
    LayoutNGShapeCache_ = value;
  }
  static bool MathMLOperatorRTLMirroringEnabled() {
    return MathMLOperatorRTLMirroring_;
  }
  static void SetMathMLOperatorRTLMirroringEnabled(bool value) {
    MathMLOperatorRTLMirroring_ = value;
  }
  static bool NoFontAntialiasingEnabled() {
    return NoFontAntialiasing_;
  }
  static void SetNoFontAntialiasingEnabled(bool value) {
    NoFontAntialiasing_ = value;
  }
  static bool ScriptRunIteratorCombiningMarkAlwaysEnabled() {
    return ScriptRunIteratorCombiningMarkAlways_;
  }
  static void SetScriptRunIteratorCombiningMarkAlwaysEnabled(bool value) {
    ScriptRunIteratorCombiningMarkAlways_ = value;
  }
  static bool ScriptRunIteratorCombiningMarksEnabled() {
    return ScriptRunIteratorCombiningMarks_;
  }
  static void SetScriptRunIteratorCombiningMarksEnabled(bool value) {
    ScriptRunIteratorCombiningMarks_ = value;
  }
  static bool SystemFallbackEmojiVSSupportEnabled() {
    return SystemFallbackEmojiVSSupport_;
  }
  static void SetSystemFallbackEmojiVSSupportEnabled(bool value) {
    SystemFallbackEmojiVSSupport_ = value;
  }
  static bool TabSizeWithSpacingEnabled() {
    return TabSizeWithSpacing_;
  }
  static void SetTabSizeWithSpacingEnabled(bool value) {
    TabSizeWithSpacing_ = value;
  }
  static bool TabWidthNegativePositionEnabled() {
    return TabWidthNegativePosition_;
  }
  static void SetTabWidthNegativePositionEnabled(bool value) {
    TabWidthNegativePosition_ = value;
  }
  static bool TextEmphasisLetterSpacingEnabled() {
    return TextEmphasisLetterSpacing_;
  }
  static void SetTextEmphasisLetterSpacingEnabled(bool value) {
    TextEmphasisLetterSpacing_ = value;
  }
  static bool TextSpacingTrimFallbackEnabled() {
    return TextSpacingTrimFallback_;
  }
  static void SetTextSpacingTrimFallbackEnabled(bool value) {
    TextSpacingTrimFallback_ = value;
  }
  static bool TextSpacingTrimFallback2Enabled() {
    if (!TextSpacingTrimFallbackEnabled()) return false;
    return TextSpacingTrimFallback2_;
  }
  static void SetTextSpacingTrimFallback2Enabled(bool value) {
    TextSpacingTrimFallback2_ = value;
  }

private:
  inline static bool BidiCaretAffinity_ = false;
  inline static bool CanvasTextCacheLimit_ = true;
  inline static bool ShapeResultCachedPreviousSafeToBreakOffset_ = true;
  inline static bool CSSChUnitSpecCompliantFallback_ = true;
  inline static bool CSSHexAlphaColor_ = true;
  inline static bool FontMatchAliasesAsLastResort_ = true;
  inline static bool FontPresentWin_ = true;
  inline static bool FontSrcLocalMatching_ = true;
  inline static bool FontSystemFallbackNotoCjk_ = true;
  inline static bool IgnoreLetterSpacingInCursiveScripts_ = true;
  inline static bool LayoutNGShapeCache_ = true;
  inline static bool MathMLOperatorRTLMirroring_ = false;
  inline static bool NoFontAntialiasing_ = false;
  inline static bool ScriptRunIteratorCombiningMarkAlways_ = true;
  inline static bool ScriptRunIteratorCombiningMarks_ = true;
  inline static bool SystemFallbackEmojiVSSupport_ = true;
  inline static bool TabSizeWithSpacing_ = true;
  inline static bool TabWidthNegativePosition_ = true;
  inline static bool TextEmphasisLetterSpacing_ = true;
  inline static bool TextSpacingTrimFallback_ = true;
  inline static bool TextSpacingTrimFallback2_ = false;
};

} // namespace bkit
