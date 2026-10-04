// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Port of platform/testing/font_test_helpers.cc and unit_test_helpers.cc.
#include "font_test_helpers.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include "font/font_custom_platform_data.h"
#include "font/font_selector.h"

namespace bkfont::test {
namespace {
[[noreturn]] void FailToLoadTestFont(const char* reason, const String& path) {
  std::fprintf(stderr, "%s: %s\n", reason, path.Utf8().c_str());
  std::abort();
}

String SourcePath(const char* relative, const String& suffix = String()) {
  std::filesystem::path path(CHROMIUM_SOURCE_DIR);
  path /= relative;
  if (!suffix.empty()) path /= std::filesystem::u8path(suffix.Utf8().c_str());
  return String::FromUTF8(path.generic_string());
}

class TestFontSelector final : public FontSelector {
public:
  static std::shared_ptr<TestFontSelector> Create(base::span<const uint8_t> data) {
    String ots_parse_message;
    auto custom = FontCustomPlatformData::Create(data, ots_parse_message);
    if (!custom) return nullptr;
    return std::make_shared<TestFontSelector>(std::move(custom));
  }
  static std::shared_ptr<TestFontSelector> Create(const String& path) {
    auto data = ReadFromFile(path);
    if (!data) FailToLoadTestFont("Cannot read test font", path);
    auto selector = Create(base::as_byte_span(*data));
    if (!selector) FailToLoadTestFont("Cannot decode test font", path);
    return selector;
  }
  explicit TestFontSelector(std::shared_ptr<FontCustomPlatformData> custom)
      : custom_platform_data_(std::move(custom)) {
  }
  std::shared_ptr<const FontData> GetFontData(
      const FontDescription& description, const FontFamily&) override {
    FontSelectionCapabilities normal_capabilities(
        {kNormalWidthValue, kNormalWidthValue},
        {kNormalSlopeValue, kNormalSlopeValue},
        {kNormalWeightValue, kNormalWeightValue});
    auto platform_data = custom_platform_data_->GetFontPlatformData(
        description.EffectiveFontSize(),
        description.AdjustedSpecifiedSize(),
        description.IsSyntheticBold() && description.SyntheticBoldAllowed(),
        description.IsSyntheticItalic() && description.SyntheticItalicAllowed(),
        description.GetFontSelectionRequest(),
        normal_capabilities,
        description.FontOpticalSizing(),
        description.TextRendering(),
        {},
        description.Orientation());
    return std::make_shared<SimpleFontData>(
        std::move(platform_data),
        std::make_shared<CustomFontData>());
  }
  void WillUseFontData(const FontDescription&, const FontFamily&, const String&) override {
  }
  void WillUseRange(const FontDescription&, const AtomicString&, const FontDataForRangeSet&) override {
  }
  unsigned Version() const override {
    return 0;
  }
  void FontCacheInvalidated() override {
  }
  void ReportSuccessfulFontFamilyMatch(const AtomicString&) override {
  }
  void ReportFailedFontFamilyMatch(const AtomicString&) override {
  }
  void ReportSuccessfulLocalFontMatch(const AtomicString&) override {
  }
  void ReportFailedLocalFontMatch(const AtomicString&) override {
  }
  void ReportNotDefGlyph() const override {
  }
  void ReportEmojiSegmentGlyphCoverage(unsigned, unsigned) override {
  }
  ExecutionContext* GetExecutionContext() const override {
    return nullptr;
  }
  FontFaceCache* GetFontFaceCache() override {
    return nullptr;
  }
  void RegisterForInvalidationCallbacks(FontSelectorClient*) override {
  }
  void UnregisterForInvalidationCallbacks(FontSelectorClient*) override {
  }
  bool IsPlatformFamilyMatchAvailable(const FontDescription&, const FontFamily&) override {
    return false;
  }

private:
  std::shared_ptr<FontCustomPlatformData> custom_platform_data_;
};
} // namespace

String BlinkRootDir() {
  return SourcePath("third_party/blink");
}
String BlinkWebTestsDir() {
  return SourcePath("third_party/blink/web_tests");
}
String PlatformTestDataPath(const String& relative_path) {
  return SourcePath("third_party/blink/renderer/platform/testing/data", relative_path);
}
String BlinkWebTestsFontsTestDataPath(const String& relative_path) {
  return SourcePath("third_party/blink/web_tests/external/wpt/fonts", relative_path);
}
String SkiaTestDataPath(const String& relative_path) {
  return SourcePath("third_party/skia/resources", relative_path);
}
std::optional<Vector<char>> ReadFromFile(const String& path) {
  std::ifstream input(std::filesystem::u8path(path.Utf8().c_str()), std::ios::binary | std::ios::ate);
  if (!input) return std::nullopt;
  const std::streampos end = input.tellg();
  if (end < 0) return std::nullopt;
  Vector<char> data(static_cast<wtf_size_t>(end));
  input.seekg(0);
  if (!input.read(data.data(), static_cast<std::streamsize>(data.size()))) return std::nullopt;
  return data;
}

std::shared_ptr<Font> CreateTestFont(
    const AtomicString& family_name, base::span<const uint8_t> data, float size,
    const FontDescription::VariantLigatures* ligatures) {
  FontDescription description;
  description.SetFamily(FontFamily(family_name, FontFamily::Type::kFamilyName));
  description.SetSpecifiedSize(size);
  description.SetComputedSize(size);
  if (ligatures) description.SetVariantLigatures(*ligatures);
  return std::make_shared<Font>(description, TestFontSelector::Create(data));
}
std::shared_ptr<Font> CreateTestFont(
    const AtomicString& family_name, const String& font_path, float size,
    const FontDescription::VariantLigatures* ligatures, FontVariantEmoji variant_emoji,
    void (*init_font_description)(FontDescription*)) {
  FontDescription description;
  description.SetFamily(FontFamily(family_name, FontFamily::Type::kFamilyName));
  description.SetSpecifiedSize(size);
  description.SetComputedSize(size);
  description.SetVariantEmoji(variant_emoji);
  if (ligatures) description.SetVariantLigatures(*ligatures);
  if (init_font_description) init_font_description(&description);
  return std::make_shared<Font>(description, TestFontSelector::Create(font_path));
}
std::shared_ptr<Font> CreateAhemFont(float size) {
  return CreateTestFont(AtomicString("Ahem"), PlatformTestDataPath("Ahem.woff"), size);
}
void TestFontPrewarmer::PrewarmFamily(const String& family_name) {
  family_names_.push_back(family_name);
}
ScopedTestFontPrewarmer::ScopedTestFontPrewarmer()
    : saved_(FontCache::GetFontPrewarmer()) {
  FontCache::SetFontPrewarmer(&current_);
}
ScopedTestFontPrewarmer::~ScopedTestFontPrewarmer() {
  FontCache::SetFontPrewarmer(saved_);
}
} // namespace bkfont::test
