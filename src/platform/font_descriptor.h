// Ported from: skia/src/core/SkFontDescriptor.h

#pragma once

#include <cstdint>
#include <memory>

#include "base/vector.h"
#include "font_arguments.h"
#include "font_style.h"
#include "stream.h"

namespace bkfont {

// SkFontData. Named FontStreamData because Blink's FontData already exists in
// this namespace.
class FontStreamData {
public:
  // Makes a copy of the data in 'axis'.
  FontStreamData(std::unique_ptr<StreamAsset> stream, int index, int palette_index,
                 const std::int32_t* axis, int axis_count,
                 const FontArguments::Palette::Override* palette_overrides, int palette_override_count)
      : stream_(std::move(stream)),
        index_(index),
        palette_index_(palette_index),
        axis_count_(axis_count),
        palette_override_count_(palette_override_count),
        axis_(static_cast<wtf_size_t>(axis_count_)),
        palette_overrides_(static_cast<wtf_size_t>(palette_override_count_)) {
    for (int i = 0; i < axis_count_; ++i) {
      axis_[i] = axis[i];
    }
    for (int i = 0; i < palette_override_count_; ++i) {
      palette_overrides_[i] = palette_overrides[i];
    }
  }

  FontStreamData(const FontStreamData& that)
      : stream_(that.stream_->Duplicate()),
        index_(that.index_),
        palette_index_(that.palette_index_),
        axis_count_(that.axis_count_),
        palette_override_count_(that.palette_override_count_),
        axis_(static_cast<wtf_size_t>(axis_count_)),
        palette_overrides_(static_cast<wtf_size_t>(palette_override_count_)) {
    for (int i = 0; i < axis_count_; ++i) {
      axis_[i] = that.axis_[i];
    }
    for (int i = 0; i < palette_override_count_; ++i) {
      palette_overrides_[i] = that.palette_overrides_[i];
    }
  }
  FontStreamData& operator=(const FontStreamData&) = delete;

  bool HasStream() const {
    return stream_ != nullptr;
  }
  std::unique_ptr<StreamAsset> DetachStream() {
    return std::move(stream_);
  }
  StreamAsset* GetStream() {
    return stream_.get();
  }
  const StreamAsset* GetStream() const {
    return stream_.get();
  }
  int GetIndex() const {
    return index_;
  }
  int GetAxisCount() const {
    return axis_count_;
  }
  // SkFixed values.
  const std::int32_t* GetAxis() const {
    return axis_.data();
  }
  int GetPaletteIndex() const {
    return palette_index_;
  }
  int GetPaletteOverrideCount() const {
    return palette_override_count_;
  }
  const FontArguments::Palette::Override* GetPaletteOverrides() const {
    return palette_overrides_.data();
  }

private:
  std::unique_ptr<StreamAsset> stream_;
  int index_;
  int palette_index_;
  int axis_count_;
  int palette_override_count_;
  Vector<std::int32_t, 4> axis_;
  Vector<FontArguments::Palette::Override, 4> palette_overrides_;
};

// SkFontDescriptor::SkFontStyleWidthForWidthAxisValue.
FontStyle::Width FontStyleWidthForWidthAxisValue(float width);

} // namespace bkfont
