// Host replacement for the Document inputs of style resolution:
//
//   Settings              Document::GetSettings() (FontBuilder defaults)
//   FontSelector          StyleEngine::GetFontSelector()
//   RootElementStyle()    documentElement()->GetComputedStyle()
//   length conversion     StyleResolverState::CssToLengthConversionData()
//   zoom factors          the Page's device scale factor and the page zoom
//                         (LocalFrame::LayoutZoomFactor() is their product)
//
// The host owns this object. Styles and the font selector are shared through
// std::shared_ptr; there is no DOM, tree scope or GC.
#pragma once

#include <memory>
#include <utility>

#include "base/text/atomic_string.h"
#include "font/generic_font_family_settings.h"
#include "paint/color4f.h"
#include "paint/device_scale.h"
#include "style/css_to_length_conversion_data.h"

namespace bkfont {

class ComputedStyle;
class ComputedStyleBuilder;
class FontSelector;

// The local subset of core/frame/settings.json5 (with the content-layer
// WebPreferences defaults) used by style resolution.
struct Settings {
  // FontBuilder::StandardFontFamilyName() and the generic family mapping of
  // the font cache.
  GenericFontFamilySettings generic_font_family_settings;
  int default_font_size = 16;
  int default_fixed_font_size = 13;
  int minimum_font_size = 0;
  int minimum_logical_font_size = 6;
  // Document::ContentLanguage(): the locale of the initial font. Null uses
  // LayoutLocale::GetDefault().
  AtomicString content_language;
  // Host extension: the initial value of 'color'.
  Color4f initial_color = kBlackColor4f;
};

class StyleHostContext {
public:
  explicit StyleHostContext(Settings settings = {}, std::shared_ptr<FontSelector> font_selector = nullptr);

  const Settings& GetSettings() const { return settings_; }
  void SetSettings(Settings);
  FontSelector* GetFontSelector() const { return font_selector_.get(); }
  void SetFontSelector(std::shared_ptr<FontSelector>);

  // Null until the root element is resolved. Resolving the root ignores its
  // previous style here; rem uses its current font, and rlh uses the initial
  // line-height until the line-height phase has completed.
  const ComputedStyle* RootElementStyle() const { return root_element_style_.get(); }
  void SetRootElementStyle(std::shared_ptr<const ComputedStyle>);

  static bool IsValidZoom(float device_scale_factor, float page_zoom_factor);
  // Returns whether a factor changed. Invalid factors change nothing.
  bool SetZoomFactors(float device_scale_factor, float page_zoom_factor);
  const DeviceScale& GetDeviceScale() const { return device_scale_; }
  float DeviceScaleFactor() const { return device_scale_.factor; }
  float PageZoomFactor() const { return page_zoom_factor_; }
  float LayoutZoomFactor() const { return device_scale_.factor * page_zoom_factor_; }

  // StyleResolver::InitialStyle(), created from the settings, the font
  // selector and the zoom factors on first use after a change.
  const ComputedStyle& InitialStyle() const;
  // For font data changes that are not visible in the settings.
  void InvalidateInitialStyle() { initial_style_.reset(); }

private:
  Settings settings_;
  std::shared_ptr<FontSelector> font_selector_;
  std::shared_ptr<const ComputedStyle> root_element_style_;
  DeviceScale device_scale_;
  float page_zoom_factor_ = 1;
  mutable std::shared_ptr<const ComputedStyle> initial_style_;
};

} // namespace bkfont
