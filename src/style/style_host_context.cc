#include "style_host_context.h"

#include <cmath>
#include <utility>

#include "style/computed_style.h"
#include "style/style_resolver.h"

namespace bkit {

StyleHostContext::StyleHostContext(Settings settings, std::shared_ptr<FontSelector> font_selector)
    : settings_(std::move(settings)), font_selector_(std::move(font_selector)) {}

void StyleHostContext::SetSettings(Settings settings) {
  settings_ = std::move(settings);
  initial_style_.reset();
}
void StyleHostContext::SetFontSelector(std::shared_ptr<FontSelector> font_selector) {
  font_selector_ = std::move(font_selector);
  initial_style_.reset();
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
