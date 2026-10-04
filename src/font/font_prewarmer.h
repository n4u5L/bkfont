// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Source: third_party/blink/public/platform/web_font_prewarmer.h.
// The native boundary uses WTF String instead of the public WebString wrapper.
#pragma once
#include "base/text/wtf_string.h"
namespace bkfont {
class FontPrewarmer {
public:
  virtual void PrewarmFamily(const String& family_name) = 0;
};
} // namespace bkfont
