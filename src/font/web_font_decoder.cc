// Ported from: blink/renderer/platform/fonts/web_font_decoder.cc
/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "web_font_decoder.h"

#include <hb.h>
#include <stdarg.h>
#include <memory>

#include "font_cache.h"
#include "base/text/wtf_string.h"
#include <ots-memory-stream.h>

namespace bkfont {

namespace {

#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
const size_t kMaxDecompressedSizeMb = 30;
#else
const size_t kMaxDecompressedSizeMb = 128;
#endif

class BlinkOTSContext final : public ots::OTSContext {

public:
  void Message(int level, const char* format, ...) override;
  ots::TableAction GetTableAction(uint32_t tag) override;
  const String& GetErrorString() {
    return accumulated_error_string_;
  }

private:
  void AppendErrorMessage(const String&& new_error_string);

  String accumulated_error_string_;
};

void BlinkOTSContext::Message(int level, const char* format, ...) {
  va_list args;
  va_start(args, format);

#if defined(COMPILER_MSVC)
  int result = _vscprintf(format, args);
#else
  char ch;
  int result = vsnprintf(&ch, 1, format, args);
#endif
  va_end(args);

  if (result <= 0) {
    AppendErrorMessage(String("Unspecified OTS Error"));
  } else {
    const unsigned len = result;
    const size_t buffer_length = static_cast<size_t>(len) + 1;
    auto buffer = std::make_unique<char[]>(buffer_length);

    va_start(args, format);
    vsnprintf(buffer.get(), buffer_length, format, args);
    va_end(args);

    AppendErrorMessage(
        String(std::span<const char>(buffer.get(), len)));
  }
}

void BlinkOTSContext::AppendErrorMessage(const String&& new_error_string) {
  if (accumulated_error_string_.empty()) {
    accumulated_error_string_ = new_error_string;
  } else {
    if (accumulated_error_string_.Contains(new_error_string)) {
      return;
    }
    accumulated_error_string_ =
        accumulated_error_string_ + "\n" + new_error_string;
  }
}

#if !defined(HB_VERSION_ATLEAST)
#define HB_VERSION_ATLEAST(major, minor, micro) 0
#endif

ots::TableAction BlinkOTSContext::GetTableAction(uint32_t tag) {
  const uint32_t kCbdtTag = OTS_TAG('C', 'B', 'D', 'T');
  const uint32_t kCblcTag = OTS_TAG('C', 'B', 'L', 'C');
  const uint32_t kColrTag = OTS_TAG('C', 'O', 'L', 'R');
  const uint32_t kCpalTag = OTS_TAG('C', 'P', 'A', 'L');
  const uint32_t kCff2Tag = OTS_TAG('C', 'F', 'F', '2');
  const uint32_t kSbixTag = OTS_TAG('s', 'b', 'i', 'x');
  const uint32_t kStatTag = OTS_TAG('S', 'T', 'A', 'T');
#if HB_VERSION_ATLEAST(1, 0, 0)
  const uint32_t kBaseTag = OTS_TAG('B', 'A', 'S', 'E');
  const uint32_t kGdefTag = OTS_TAG('G', 'D', 'E', 'F');
  const uint32_t kGposTag = OTS_TAG('G', 'P', 'O', 'S');
  const uint32_t kGsubTag = OTS_TAG('G', 'S', 'U', 'B');

  // Font Variations related tables
  // See "Variation Tables" in Terminology section of
  // https://www.microsoft.com/typography/otspec/otvaroverview.htm
  const uint32_t kAvarTag = OTS_TAG('a', 'v', 'a', 'r');
  const uint32_t kCvarTag = OTS_TAG('c', 'v', 'a', 'r');
  const uint32_t kFvarTag = OTS_TAG('f', 'v', 'a', 'r');
  const uint32_t kGvarTag = OTS_TAG('g', 'v', 'a', 'r');
  const uint32_t kHvarTag = OTS_TAG('H', 'V', 'A', 'R');
  const uint32_t kMvarTag = OTS_TAG('M', 'V', 'A', 'R');
  const uint32_t kVvarTag = OTS_TAG('V', 'V', 'A', 'R');
#endif

  switch (tag) {
  // Google Color Emoji Tables
  case kCbdtTag:
  case kCblcTag:
  // Windows Color Emoji Tables
  case kColrTag:
  case kCpalTag:
  case kCff2Tag:
  case kSbixTag:
  case kStatTag:
#if HB_VERSION_ATLEAST(1, 0, 0)
  // Let HarfBuzz handle how to deal with broken tables.
  case kAvarTag:
  case kBaseTag:
  case kCvarTag:
  case kFvarTag:
  case kGvarTag:
  case kHvarTag:
  case kMvarTag:
  case kVvarTag:
  case kGdefTag:
  case kGposTag:
  case kGsubTag:
#endif
    return ots::TABLE_ACTION_PASSTHRU;
  default:
    return ots::TABLE_ACTION_DEFAULT;
  }
}

} // namespace

std::shared_ptr<Typeface> WebFontDecoder::Decode(std::span<const uint8_t> buffer) {
  if (!buffer.data()) {
    SetErrorString("Empty Buffer");
    return nullptr;
  }

  // This is the largest web font size which we'll try to transcode.
  static const size_t kMaxDecompressedSize =
      kMaxDecompressedSizeMb * 1024 * 1024;
  if (buffer.size() > kMaxDecompressedSize) {
    String error_message =
        String::Format("Web font size more than %zuMB", kMaxDecompressedSizeMb);
    SetErrorString(error_message.Utf8().c_str());
    return nullptr;
  }

  // Most web fonts are compressed, so the result can be much larger than
  // the original.
  ots::ExpandingMemoryStream output(buffer.size(), kMaxDecompressedSize);
  BlinkOTSContext ots_context;

  bool ok = ots_context.Process(
      &output,
      buffer.data(),
      buffer.size());

  if (!ok) {
    SetErrorString(ots_context.GetErrorString());
    return nullptr;
  }

  const size_t decoded_length = static_cast<size_t>(output.Tell());
  // The OTS stream is local; the typeface must own the sanitized bytes.
  auto new_typeface = FontCache::Get().GetFontManager()->MakeFromData(
      Data::MakeWithCopy(output.get(), decoded_length));
  if (!new_typeface) {
    SetErrorString("Unable to instantiate font face from font data.");
    return nullptr;
  }
  decoded_size_ = decoded_length;

  return new_typeface;
}

} // namespace bkfont
