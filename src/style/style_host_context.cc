// Ported from: blink/renderer/core/css/css_font_selector.cc
// Ported from: blink/common/web_preferences/web_preferences.cc
// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "style_host_context.h"

#include <cmath>
#include <utility>

#include "font/font_cache.h"
#include "font/font_description.h"
#include "font/font_family.h"
#include "font/font_selector.h"
#include "font/font_selector_client.h"
#include "font/simple_font_data.h"
#include "font/target_platform.h"
#include "style/computed_style.h"
#include "style/font_size_functions.h"
#include "style/style_resolver.h"

namespace bkit {

namespace {

// CSSFontSelector::GetFontData(): unresolved generic families use this
// document's settings after any selection supplied by the host.
// Keep a settings snapshot so fonts retained by an old layout stay valid
// when another host changes its preferences or custom font selector.
class StyleFontSelector final : public FontSelector, private FontSelectorClient {
public:
  StyleFontSelector(const GenericFontFamilySettings& settings, std::shared_ptr<FontSelector> custom)
      : settings_(settings), custom_(std::move(custom)) {
    if (custom_) custom_->RegisterForInvalidationCallbacks(this);
  }
  ~StyleFontSelector() override {
    if (custom_) custom_->UnregisterForInvalidationCallbacks(this);
  }

  std::shared_ptr<const FontData> GetFontData(const FontDescription& description, const FontFamily& family) override {
    if (custom_) {
      if (auto data = custom_->GetFontData(description, family)) return data;
    }
    const AtomicString name = FamilyNameFromSettings(settings_, description, family, nullptr);
    if (name.empty()) return nullptr;
    auto data = FontCache::Get().GetFontData(description, name);
    if (data && description.HasSizeAdjust()) {
      if (auto size = FontSizeFunctions::MetricsMultiplierAdjustedFontSize(data.get(), description)) {
        FontDescription adjusted(description);
        adjusted.SetAdjustedSize(*size);
        data = FontCache::Get().GetFontData(adjusted, name);
      }
    }
    return data;
  }

  bool IsPlatformFamilyMatchAvailable(const FontDescription& description, const FontFamily& family) override {
    if (custom_ && custom_->IsPlatformFamilyMatchAvailable(description, family)) return true;
    const AtomicString name = FamilyNameFromSettings(settings_, description, family, nullptr);
    if (!name.empty()) return FontCache::Get().IsPlatformFamilyMatchAvailable(description, name);
    return FontCache::Get().IsPlatformFamilyMatchAvailable(description, family.FamilyName());
  }
  void WillUseFontData(const FontDescription& description, const FontFamily& family, const String& text) override {
    if (custom_) custom_->WillUseFontData(description, family, text);
  }
  void WillUseRange(const FontDescription& description, const AtomicString& family, const FontDataForRangeSet& range) override {
    if (custom_) custom_->WillUseRange(description, family, range);
  }
  unsigned Version() const override { return custom_ ? custom_->Version() : 0; }
  void ReportSuccessfulFontFamilyMatch(const AtomicString& name) override {
    if (custom_) custom_->ReportSuccessfulFontFamilyMatch(name);
  }
  void ReportFailedFontFamilyMatch(const AtomicString& name) override {
    if (custom_) custom_->ReportFailedFontFamilyMatch(name);
  }
  void ReportSuccessfulLocalFontMatch(const AtomicString& name) override {
    if (custom_) custom_->ReportSuccessfulLocalFontMatch(name);
  }
  void ReportFailedLocalFontMatch(const AtomicString& name) override {
    if (custom_) custom_->ReportFailedLocalFontMatch(name);
  }
  void ReportNotDefGlyph() const override {
    if (custom_) custom_->ReportNotDefGlyph();
  }
  void ReportEmojiSegmentGlyphCoverage(unsigned clusters, unsigned broken) override {
    if (custom_) custom_->ReportEmojiSegmentGlyphCoverage(clusters, broken);
  }
  void RegisterForInvalidationCallbacks(FontSelectorClient* client) override {
    if (!clients_.Contains(client)) clients_.push_back(client);
  }
  void UnregisterForInvalidationCallbacks(FontSelectorClient* client) override {
    const wtf_size_t index = clients_.Find(client);
    if (index != kNotFound) clients_.EraseAt(index);
  }
  void FontFaceInvalidated(FontInvalidationReason reason) override {
    if (custom_) custom_->FontFaceInvalidated(reason);
  }
  void FontCacheInvalidated() override { FontsNeedUpdate(this, FontInvalidationReason::kGeneralInvalidation); }
  ExecutionContext* GetExecutionContext() const override { return custom_ ? custom_->GetExecutionContext() : nullptr; }
  FontFaceCache* GetFontFaceCache() override { return custom_ ? custom_->GetFontFaceCache() : nullptr; }

private:
  void FontsNeedUpdate(FontSelector*, FontInvalidationReason reason) override {
    const auto clients = clients_;
    for (FontSelectorClient* client : clients) client->FontsNeedUpdate(this, reason);
  }
  GenericFontFamilySettings settings_;
  std::shared_ptr<FontSelector> custom_;
  Vector<FontSelectorClient*> clients_;
};

} // namespace

// blink::web_pref::WebPreferences(), common-script defaults. Script-specific
// preferences supplied through Settings override these via the same lookup.
GenericFontFamilySettings Settings::DefaultGenericFontFamilySettings() {
  GenericFontFamilySettings settings;
  settings.UpdateStandard(AtomicString("Times New Roman"));
  settings.UpdateSerif(AtomicString("Times New Roman"));
  settings.UpdateSansSerif(AtomicString("Arial"));
#if BUILDFLAG(IS_MAC)
  settings.UpdateFixed(AtomicString("Menlo"));
#else
  settings.UpdateFixed(AtomicString("Courier New"));
#endif
  settings.UpdateCursive(AtomicString("Script"));
  settings.UpdateFantasy(AtomicString("Impact"));
  settings.UpdateMath(AtomicString("Latin Modern Math"));
  return GenericFontFamilySettings(settings);
}

StyleHostContext::StyleHostContext(Settings settings, std::shared_ptr<FontSelector> font_selector)
    : settings_(std::move(settings)), custom_font_selector_(std::move(font_selector)) {
  UpdateFontSelector();
}

void StyleHostContext::UpdateFontSelector() {
  font_selector_ = std::make_shared<StyleFontSelector>(settings_.generic_font_family_settings, custom_font_selector_);
  initial_style_.reset();
}

void StyleHostContext::SetSettings(Settings settings) {
  settings_ = std::move(settings);
  UpdateFontSelector();
}
void StyleHostContext::SetFontSelector(std::shared_ptr<FontSelector> font_selector) {
  custom_font_selector_ = std::move(font_selector);
  UpdateFontSelector();
}
void StyleHostContext::SetRootElementStyle(std::shared_ptr<const ComputedStyle> style) {
  root_element_style_ = std::move(style);
}

bool StyleHostContext::IsValidZoom(float device_scale_factor, float page_zoom_factor) {
  const float zoom = device_scale_factor * page_zoom_factor;
  return DeviceScale{device_scale_factor}.IsValid() && std::isfinite(page_zoom_factor) && page_zoom_factor > 0 &&
         std::isfinite(zoom) && zoom > 0;
}
bool StyleHostContext::SetZoomFactors(float device_scale_factor, float page_zoom_factor) {
  if (!IsValidZoom(device_scale_factor, page_zoom_factor)) return false;
  if (device_scale_.factor == device_scale_factor && page_zoom_factor_ == page_zoom_factor) return false;
  device_scale_ = DeviceScale{device_scale_factor};
  page_zoom_factor_ = page_zoom_factor;
  initial_style_.reset();
  return true;
}

const ComputedStyle& StyleHostContext::InitialStyle() const {
  if (!initial_style_) initial_style_ = StyleResolver::CreateInitialStyle(*this);
  return *initial_style_;
}

} // namespace bkit
