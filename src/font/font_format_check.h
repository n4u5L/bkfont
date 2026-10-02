// Port source: third_party/blink/renderer/platform/fonts/opentype/font_format_check.h
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <span>

#include "platform/font_face.h"
namespace blink {

class FontFormatCheck {

public:
  explicit FontFormatCheck(std::span<const uint8_t>);
  FontFormatCheck(const FontFormatCheck&);
  FontFormatCheck& operator=(const FontFormatCheck&);
  virtual ~FontFormatCheck() = default;
  virtual bool IsVariableFont() const;
  virtual bool IsCbdtCblcColorFont() const;
  virtual bool IsColrCpalColorFont() const {
    return IsColrCpalColorFontV0() || IsColrCpalColorFontV1();
  }
  virtual bool IsColrCpalColorFontV0() const;
  virtual bool IsColrCpalColorFontV1() const;
  bool IsVariableColrV0Font() const;
  virtual bool IsSbixColorFont() const;
  virtual bool IsCff2OutlineFont() const;
  bool IsColorFont() const;

  // Still needed in FontCustomPlatformData.
  enum class VariableFontSubType {
    kNotVariable,
    kVariableTrueType,
    kVariableCFF2
  };

  static VariableFontSubType ProbeVariableFont(std::shared_ptr<FontFace>);

  enum class COLRVersion {
    kCOLRV0,
    kCOLRV1,
    kNoCOLR
  };

private:
  std::span<const uint32_t> TableTags() const {
    return std::span<const uint32_t>(table_tags_.get(), table_tags_count_);
  }
  std::unique_ptr<uint32_t[]> table_tags_;
  size_t table_tags_count_ = 0;
  COLRVersion colr_version_ = COLRVersion::kNoCOLR;
};

} // namespace blink
