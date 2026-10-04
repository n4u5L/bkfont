// Source: third_party/blink/renderer/platform/wtf/text/string_view.cc
// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/text/string_view.h"

#include <unicode/utf16.h>
#include <array>
#include <memory>

#include "base/text/ascii_fast_path.h"
#include "base/text/atomic_string.h"
#include "base/text/character_names.h"
#include "base/text/character_visitor.h"
#include "base/text/code_point_iterator.h"
#include "base/text/string_builder.h"
#include "base/text/string_impl.h"
#include "base/text/utf16.h"
#include "base/text/utf8.h"
#include "base/text/wtf_string.h"

namespace bkfont {

namespace {
class StackStringViewAllocator {
public:
  explicit StackStringViewAllocator(
      StringView::StackBackingStore& backing_store)
      : backing_store_(backing_store) {
  }
  using ResultStringType = StringView;

  template <typename CharType>
  StringView Alloc(wtf_size_t length, base::span<CharType>& buffer) {
    buffer = backing_store_.Realloc<CharType>(length);
    return StringView(buffer);
  }

  StringView CoerceOriginal(StringView string) {
    return string;
  }

private:
  StringView::StackBackingStore& backing_store_;
};
} // namespace

StringView::StringView(const UChar* chars)
    // SAFETY: It's safe if `chars` points to a NUL-terminated string.
    : StringView(UNSAFE_BUFFERS(
          base::span(chars, chars ? LengthOfNullTerminatedString(chars) : 0))) {
}

#if 0
StringView::~StringView() {
  ;
  ;
}
#endif

// Helper to write a three-byte UTF-8 code point to the buffer, caller must
// check room is available.
static inline void PutUTF8Triple(base::span<uint8_t, 3u> buffer, UChar ch) {
  ;
  buffer[0] = ((ch >> 12) & 0x0F) | 0xE0;
  buffer[1] = ((ch >> 6) & 0x3F) | 0x80;
  buffer[2] = (ch & 0x3F) | 0x80;
}

std::string StringView::Utf8(Utf8ConversionMode mode) const {
  using unicode::ConversionResult;
  using unicode::ConversionStatus;
  unsigned length = this->length();

  if (!length)
    return std::string();

  // Allocate a buffer big enough to hold all the characters
  // (an individual UTF-16 UChar can only expand to 3 UTF-8 bytes).
  // Optimization ideas, if we find this function is hot:
  //  * We could speculatively create a std::string to contain 'length'
  //    characters, and resize if necessary (i.e. if the buffer contains
  //    non-ascii characters). (Alternatively, scan the buffer first for
  //    ascii characters, so we know this will be sufficient).
  //  * We could allocate a std::string with an appropriate size to
  //    have a good chance of being able to write the string into the
  //    buffer without reallocing (say, 1.5 x length).
  if (length > std::numeric_limits<unsigned>::max() / 3)
    return std::string();
  std::array<char, 1024> stack_buffer{};
  auto heap_buffer = length * 3 > stack_buffer.size()
                         ? std::make_unique<char[]>(length * 3)
                         : nullptr;
  base::span<char> conversion_buffer(
      heap_buffer ? heap_buffer.get() : stack_buffer.data(),
      length * 3);
  size_t buffer_written = 0;

  if (Is8Bit()) {
    ConversionResult result = unicode::ConvertLatin1ToUtf8(
        Span8(),
        base::as_writable_byte_span(conversion_buffer));
    // (length * 3) should be sufficient for any conversion
    ;
    buffer_written = result.converted.size();
  } else {
    base::span<const UChar> characters = Span16();
    base::span<uint8_t> buffer(base::as_writable_byte_span(conversion_buffer));

    if (mode == Utf8ConversionMode::kStrictReplacingErrors) {
      while (!characters.empty()) {
        // Use strict conversion to detect unpaired surrogates.
        ConversionResult result =
            unicode::ConvertUtf16ToUtf8(characters, buffer, true);
        ;
        buffer = buffer.subspan(result.converted.size());
        // Conversion fails when there is an unpaired surrogate.  Put
        // replacement character (U+FFFD) instead of the unpaired
        // surrogate.
        if (result.status != ConversionStatus::kConversionOK) {
          ;
          ;
          // There should be room left, since one UChar hasn't been
          // converted.
          auto [replacement_buffer, rest] = buffer.split_at<3u>();
          PutUTF8Triple(replacement_buffer, uchar::kReplacementCharacter);
          buffer = rest;
          result.consumed++;
        }
        characters = characters.subspan(result.consumed);
      }
      buffer_written = conversion_buffer.size() - buffer.size();
    } else {
      const bool strict = mode == Utf8ConversionMode::kStrict;

      ConversionResult result =
          unicode::ConvertUtf16ToUtf8(characters, buffer, strict);
      // (length * 3) should be sufficient for any conversion
      ;

      // Only produced from strict conversion.
      if (result.status == ConversionStatus::kSourceIllegal) {
        ;
        return std::string();
      }

      // Check for an unconverted high surrogate.
      if (result.status == ConversionStatus::kSourceExhausted) {
        if (strict)
          return std::string();
        buffer = buffer.subspan(result.converted.size());

        // This should be one unpaired high surrogate. Treat it the same
        // was as an unpaired high surrogate would have been handled in
        // the middle of a string with non-strict conversion - which is
        // to say, simply encode it to UTF-8.
        ;
        ;
        ;
        // There should be room left, since one UChar hasn't been
        // converted.
        auto unpaired_surrogate_buffer = buffer.first<3u>();
        PutUTF8Triple(unpaired_surrogate_buffer, characters[result.consumed]);
        buffer_written = unpaired_surrogate_buffer.size();
      }
      buffer_written += result.converted.size();
    }
  }
  return std::string(conversion_buffer.data(), buffer_written);
}

bool StringView::IsLowerASCII() const {
  if (StringImpl* impl = SharedImpl()) {
    return impl->IsLowerASCII();
  }
  return VisitCharacters(*this, [](auto chars) { return IsLowerAscii(chars); });
}

bool StringView::ContainsOnlyASCIIOrEmpty() const {
  if (StringImpl* impl = SharedImpl())
    return impl->ContainsOnlyASCIIOrEmpty();
  if (empty())
    return true;
  AsciiStringAttributes attrs = VisitCharacters(
      *this,
      [](auto chars) { return CharacterAttributes(chars); });
  return attrs.contains_only_ascii;
}

bool StringView::SubstringContainsOnlyWhitespaceOrEmpty(unsigned from,
                                                        unsigned to) const {
  ;
  return VisitCharacters(StringView(*this, from, to - from), [](auto chars) {
    for (size_t i = 0; i < chars.size(); ++i) {
      if (!IsASCIISpace(chars[i])) {
        return false;
      }
    }
    return true;
  });
}

String StringView::ToString() const {
  if (IsNull())
    return String();
  if (empty())
    return g_empty_string;
  if (StringImpl* impl = SharedImpl())
    return impl;
  if (Is8Bit())
    return String(Span8());
  return StringImpl::Create8BitIfPossible(Span16());
}

AtomicString StringView::ToAtomicString() const {
  if (IsNull())
    return g_null_atom;
  if (empty())
    return g_empty_atom;
  if (StringImpl* impl = SharedImpl())
    return AtomicString(impl);
  if (Is8Bit())
    return AtomicString(Span8());
  return AtomicString(Span16());
}

String StringView::EncodeForDebugging() const {
  if (IsNull()) {
    return "<null>";
  }

  StringBuilder builder;
  builder.Append('"');
  for (unsigned index = 0; index < length(); ++index) {
    // Print shorthands for select cases.
    UChar character = (*this)[index];
    switch (character) {
    case '\t':
      builder.Append("\\t");
      break;
    case '\n':
      builder.Append("\\n");
      break;
    case '\r':
      builder.Append("\\r");
      break;
    case '"':
      builder.Append("\\\"");
      break;
    case '\\':
      builder.Append("\\\\");
      break;
    default:
      if (IsASCIIPrintable(character)) {
        builder.Append(static_cast<char>(character));
      } else {
        // Print "\uXXXX" for control or non-ASCII characters.
        builder.AppendFormat("\\u%04X", character);
      }
      break;
    }
  }
  builder.Append('"');
  return builder.ToString();
}

bool EqualStringView(const StringView& a, const StringView& b) {
  if (a.IsNull() || b.IsNull())
    return a.IsNull() == b.IsNull();
  if (a.length() != b.length())
    return false;
  if (a.Bytes() == b.Bytes() && a.Is8Bit() == b.Is8Bit())
    return true;
  return VisitCharacters(a, [b](auto chars) {
    return b.Is8Bit() ? chars == b.Span8() : chars == b.Span16();
  });
}

bool DeprecatedEqualIgnoringCaseAndNullity(const StringView& a,
                                           const StringView& b) {
  if (a.length() != b.length())
    return false;
  return VisitCharacters(a, [b](auto chars) {
    return b.Is8Bit() ? DeprecatedEqualIgnoringCase(chars, b.Span8())
                      : DeprecatedEqualIgnoringCase(chars, b.Span16());
  });
}

bool DeprecatedEqualIgnoringCase(const StringView& a, const StringView& b) {
  if (a.IsNull() || b.IsNull())
    return a.IsNull() == b.IsNull();
  return DeprecatedEqualIgnoringCaseAndNullity(a, b);
}

bool EqualIgnoringASCIICase(const StringView& a, const StringView& b) {
  if (a.IsNull() || b.IsNull())
    return a.IsNull() == b.IsNull();
  if (a.length() != b.length())
    return false;
  if (a.Bytes() == b.Bytes() && a.Is8Bit() == b.Is8Bit())
    return true;
  return VisitCharacters(a, [b](auto chars) {
    return b.Is8Bit() ? EqualIgnoringASCIICase(chars, b.Span8())
                      : EqualIgnoringASCIICase(chars, b.Span16());
  });
}

StringView StringView::LowerASCIIMaybeUsingBuffer(
    StackBackingStore& buffer) const {
  return ConvertAsciiCase(*this, LowerConverter(), StackStringViewAllocator(buffer));
}

UChar32 StringView::CodepointAt(unsigned i) const {
  ;
  if (Is8Bit())
    return (*this)[i];
  return bkfont::CodePointAt(Span16(), i);
}

unsigned StringView::NextCodePointOffset(unsigned i) const {
  ;
  unsigned next = i + 1;
  if (Is8Bit())
    return next;
  auto str = Span16();
  if (U16_IS_LEAD(str[i]) && next < str.size() && U16_IS_TRAIL(str[next])) {
    ++next;
  }
  return next;
}

UChar32 StringView::CodePointAtAndNext(unsigned& i) const {
  if (Is8Bit()) {
    return (*this)[i++];
  }
  return bkfont::CodePointAtAndNext(Span16(), i);
}

CodePointIterator StringView::begin() const {
  return CodePointIterator(*this);
}

CodePointIterator StringView::end() const {
  return CodePointIterator::End(*this);
}

std::ostream& operator<<(std::ostream& out, const StringView& string) {
  return out << string.EncodeForDebugging().Utf8();
}

} // namespace bkfont
