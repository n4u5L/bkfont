// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Port of platform/testing/font_test_helpers.h and unit_test_helpers.h.
#pragma once

#include <memory>
#include <optional>
#include "base/containers/span.h"
#include "font/font.h"
#include "font/font_prewarmer.h"
#include "base/vector.h"

namespace blink::test {
std::shared_ptr<Font> CreateTestFont(
    const AtomicString& family_name, const String& font_path, float size,
    const FontDescription::VariantLigatures* ligatures = nullptr,
    FontVariantEmoji variant_emoji = kNormalVariantEmoji,
    void (*init_font_description)(FontDescription*) = nullptr);
std::shared_ptr<Font> CreateTestFont(
    const AtomicString& family_name, base::span<const uint8_t> data, float size,
    const FontDescription::VariantLigatures* ligatures = nullptr);
std::shared_ptr<Font> CreateAhemFont(float size);

String BlinkRootDir();
String BlinkWebTestsDir();
String PlatformTestDataPath(const String& relative_path = String());
String BlinkWebTestsFontsTestDataPath(const String& relative_path = String());
String SkiaTestDataPath(const String& relative_path = String());
std::optional<Vector<char>> ReadFromFile(const String& path);

class TestFontPrewarmer : public FontPrewarmer {
public:
  void PrewarmFamily(const String& family_name) override;
  const Vector<String>& PrewarmedFamilyNames() const {
    return family_names_;
  }

private:
  Vector<String> family_names_;
};

class ScopedTestFontPrewarmer {
public:
  ScopedTestFontPrewarmer();
  ~ScopedTestFontPrewarmer();
  const Vector<String>& PrewarmedFamilyNames() const {
    return current_.PrewarmedFamilyNames();
  }

private:
  TestFontPrewarmer current_;
  FontPrewarmer* saved_;
};
} // namespace blink::test
