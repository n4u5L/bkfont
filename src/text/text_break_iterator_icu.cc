// Source: third_party/blink/renderer/platform/text/text_break_iterator_icu.cc
/*
 * Copyright (C) 2006 Lars Knoll <lars@trolltech.com>
 * Copyright (C) 2007, 2011, 2012 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include "text_break_iterator.h"

#include <unicode/rbbi.h>
#include <unicode/ubrk.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

#include "base/memory/ptr_util.h"
#include "icu_error.h"
#include "text_break_iterator_internal_icu.h"
#include "base/hash_map.h"
#include "base/text/atomic_string_hash.h"
#include "base/text/wtf_string.h"
#include "base/thread_specific.h"

namespace blink {

namespace {

icu::Locale LineBreakLocale(const char* locale_id, UErrorCode& status) {
  icu::Locale locale(locale_id);
  // Chromium's ICU source/data/brkitr/{root,zh,zh_Hant}.txt maps the
  // default line iterator to line_normal*.brk. Stock ICU uses the strict
  // rules there. Preserve Chromium's default without changing an explicit
  // lb keyword. Japanese and Korean must keep their default line_normal.brk:
  // their explicit lb=normal instead selects line_normal_cj.brk.
  const char* language = locale.getLanguage();
  if (std::strcmp(language, "ja") != 0 && std::strcmp(language, "ko") != 0) {
    UErrorCode keyword_status = U_ZERO_ERROR;
    const int32_t length =
        locale.getKeywordValue("lb", nullptr, 0, keyword_status);
    // The size query reports overflow when an lb value is present.
    if (U_FAILURE(keyword_status) && keyword_status != U_BUFFER_OVERFLOW_ERROR) {
      status = keyword_status;
    } else if (!length) {
      locale.setKeywordValue("lb", "normal", status);
    }
  }
  return locale;
}

class LineBreakIteratorPool final {

public:
  static LineBreakIteratorPool& SharedPool() {
    static ThreadSpecific<LineBreakIteratorPool>* pool =
        new ThreadSpecific<LineBreakIteratorPool>;
    return **pool;
  }

  LineBreakIteratorPool() = default;
  LineBreakIteratorPool(const LineBreakIteratorPool&) = delete;
  LineBreakIteratorPool& operator=(const LineBreakIteratorPool&) = delete;

  icu::BreakIterator* Take(const AtomicString& locale) {
    icu::BreakIterator* iterator = nullptr;
    for (size_t i = 0; i < pool_size_; ++i) {
      if (pool_[i].first == locale) {
        iterator = pool_[i].second;
        EraseAt(i);
        break;
      }
    }

    if (!iterator) {
      UErrorCode open_status = U_ZERO_ERROR;
      bool locale_is_empty = locale.empty();
      iterator = icu::BreakIterator::createLineInstance(
          locale_is_empty ? LineBreakLocale(CurrentTextBreakLocaleID(), open_status)
                          : LineBreakLocale(locale.Utf8().c_str(), open_status),
          open_status);
      // locale comes from a web page and it can be invalid, leading ICU
      // to fail, in which case we fall back to the default locale.
      if (!locale_is_empty && U_FAILURE(open_status)) {
        open_status = U_ZERO_ERROR;
        iterator = icu::BreakIterator::createLineInstance(
            LineBreakLocale(CurrentTextBreakLocaleID(), open_status),
            open_status);
      }

      if (U_FAILURE(open_status)) {
        ;
        return nullptr;
      }
    }

    ;
    vended_iterators_.Set(iterator, locale);
    return iterator;
  }

  void Put(icu::BreakIterator* iterator) {
    ;

    if (pool_size_ == kCapacity) {
      delete (pool_[0].second);
      EraseAt(0);
    }

    pool_[pool_size_++] = Entry(vended_iterators_.Take(iterator), iterator);
  }

private:
  static const size_t kCapacity = 4;

  typedef std::pair<AtomicString, icu::BreakIterator*> Entry;
  // Put() evicts the oldest entry at kCapacity; this is a real upper bound.
  std::array<Entry, kCapacity> pool_{};
  size_t pool_size_ = 0;

  void EraseAt(size_t index) {
    for (size_t i = index; i + 1 < pool_size_; ++i)
      pool_[i] = std::move(pool_[i + 1]);
    pool_[--pool_size_] = Entry();
  }

  HashMap<icu::BreakIterator*, AtomicString> vended_iterators_;

  friend ThreadSpecific<LineBreakIteratorPool>::
  operator LineBreakIteratorPool*();
};

enum TextContext {
  kNoContext,
  kPriorContext,
  kPrimaryContext
};

constexpr int kTextBufferCapacity = 16;

struct UTextWithBuffer {

  UText text;
  UChar buffer[kTextBufferCapacity];
};

inline int64_t TextPinIndex(int64_t& index, int64_t limit) {
  if (index < 0) {
    index = 0;
  } else if (index > limit) {
    index = limit;
  }
  return index;
}

inline int64_t TextNativeLength(UText* text) {
  return text->a + text->b;
}

// Relocate pointer from source into destination as required.
void TextFixPointer(const UText* source,
                    UText* destination,
                    const void*& pointer) {
  if (pointer >= source->pExtra && pointer < UNSAFE_TODO(static_cast<char*>(source->pExtra) + source->extraSize)) {
    // Pointer references source extra buffer.
    pointer = UNSAFE_TODO(static_cast<char*>(destination->pExtra) + (static_cast<const char*>(pointer) - static_cast<const char*>(source->pExtra)));
  } else if (pointer >= source && pointer < UNSAFE_TODO(reinterpret_cast<const char*>(source) + source->sizeOfStruct)) {
    // Pointer references source text structure, but not source extra buffer.
    pointer = UNSAFE_TODO(reinterpret_cast<char*>(destination) + (static_cast<const char*>(pointer) - reinterpret_cast<const char*>(source)));
  }
}

UText* TextClone(UText* destination,
                 const UText* source,
                 UBool deep,
                 UErrorCode* status) {
  ;
  if (U_FAILURE(*status)) {
    return nullptr;
  }
  int32_t extra_size = source->extraSize;
  destination = utext_setup(destination, extra_size, status);
  if (U_FAILURE(*status)) {
    return destination;
  }
  void* extra_new = destination->pExtra;
  int32_t flags = destination->flags;
  int size_to_copy = std::min(source->sizeOfStruct, destination->sizeOfStruct);
  UNSAFE_TODO(memcpy(destination, source, size_to_copy));
  destination->pExtra = extra_new;
  destination->flags = flags;
  if (extra_size > 0) {
    UNSAFE_TODO(memcpy(destination->pExtra, source->pExtra, extra_size));
  }
  TextFixPointer(source, destination, destination->context);
  TextFixPointer(source, destination, destination->p);
  TextFixPointer(source, destination, destination->q);
  ;
  const void* chunk_contents =
      static_cast<const void*>(destination->chunkContents);
  TextFixPointer(source, destination, chunk_contents);
  destination->chunkContents = static_cast<const UChar*>(chunk_contents);
  return destination;
}

int32_t TextExtract(UText*,
                    int64_t,
                    int64_t,
                    UChar*,
                    int32_t,
                    UErrorCode* error_code) {
  // In the present context, this text provider is used only with ICU functions
  // that do not perform an extract operation.
  ;
  std::unreachable();
}

void TextClose(UText* text) {
  text->context = nullptr;
}

inline TextContext TextGetContext(const UText* text,
                                  int64_t native_index,
                                  UBool forward) {
  if (!text->b || native_index > text->b) {
    return kPrimaryContext;
  }
  if (native_index == text->b) {
    return forward ? kPrimaryContext : kPriorContext;
  }
  return kPriorContext;
}

inline TextContext TextLatin1GetCurrentContext(const UText* text) {
  if (!text->chunkContents) {
    return kNoContext;
  }
  return text->chunkContents == text->pExtra ? kPrimaryContext : kPriorContext;
}

void TextLatin1MoveInPrimaryContext(UText* text,
                                    int64_t native_index,
                                    int64_t native_length,
                                    UBool forward) {
  ;
  if (forward) {
    ;
    ;
    text->chunkNativeStart = native_index;
    text->chunkNativeLimit = native_index + text->extraSize / sizeof(UChar);
    if (text->chunkNativeLimit > native_length) {
      text->chunkNativeLimit = native_length;
    }
  } else {
    ;
    ;
    text->chunkNativeLimit = native_index;
    text->chunkNativeStart = native_index - text->extraSize / sizeof(UChar);
    if (text->chunkNativeStart < text->b) {
      text->chunkNativeStart = text->b;
    }
  }
  int64_t length = text->chunkNativeLimit - text->chunkNativeStart;
  // Ensure chunk length is well defined if computed length exceeds int32_t
  // range.
  ;
  text->chunkLength = length <= std::numeric_limits<int32_t>::max()
                          ? static_cast<int32_t>(length)
                          : 0;
  text->nativeIndexingLimit = text->chunkLength;
  text->chunkOffset = forward ? 0 : text->chunkLength;
  auto source = UNSAFE_TODO(base::span(
      static_cast<const LChar*>(text->p) + (text->chunkNativeStart - text->b),
      static_cast<unsigned>(text->chunkLength)));
  auto dest = UNSAFE_TODO(base::span(const_cast<UChar*>(text->chunkContents),
                                     static_cast<unsigned>(text->chunkLength)));
  StringImpl::CopyChars(dest, source);
}

void TextLatin1SwitchToPrimaryContext(UText* text,
                                      int64_t native_index,
                                      int64_t native_length,
                                      UBool forward) {
  ;
  text->chunkContents = static_cast<const UChar*>(text->pExtra);
  TextLatin1MoveInPrimaryContext(text, native_index, native_length, forward);
}

void TextLatin1MoveInPriorContext(UText* text,
                                  int64_t native_index,
                                  int64_t native_length,
                                  UBool forward) {
  ;
  ;
  ;
  ;
  text->chunkNativeStart = 0;
  text->chunkNativeLimit = text->b;
  text->chunkLength = text->b;
  text->nativeIndexingLimit = text->chunkLength;
  int64_t offset = native_index - text->chunkNativeStart;
  // Ensure chunk offset is well defined if computed offset exceeds int32_t
  // range or chunk length.
  ;
  text->chunkOffset = std::min(offset <= std::numeric_limits<int32_t>::max()
                                   ? static_cast<int32_t>(offset)
                                   : 0,
                               text->chunkLength);
}

void TextLatin1SwitchToPriorContext(UText* text,
                                    int64_t native_index,
                                    int64_t native_length,
                                    UBool forward) {
  ;
  text->chunkContents = static_cast<const UChar*>(text->q);
  TextLatin1MoveInPriorContext(text, native_index, native_length, forward);
}

inline bool TextInChunkOrOutOfRange(UText* text,
                                    int64_t native_index,
                                    int64_t native_length,
                                    UBool forward,
                                    UBool& is_accessible) {
  if (forward) {
    if (native_index >= text->chunkNativeStart && native_index < text->chunkNativeLimit) {
      int64_t offset = native_index - text->chunkNativeStart;
      // Ensure chunk offset is well formed if computed offset exceeds int32_t
      // range.
      ;
      text->chunkOffset = offset <= std::numeric_limits<int32_t>::max()
                              ? static_cast<int32_t>(offset)
                              : 0;
      is_accessible = true;
      return true;
    }
    if (native_index >= native_length && text->chunkNativeLimit == native_length) {
      text->chunkOffset = text->chunkLength;
      is_accessible = false;
      return true;
    }
  } else {
    if (native_index > text->chunkNativeStart && native_index <= text->chunkNativeLimit) {
      int64_t offset = native_index - text->chunkNativeStart;
      // Ensure chunk offset is well formed if computed offset exceeds int32_t
      // range.
      ;
      text->chunkOffset = offset <= std::numeric_limits<int32_t>::max()
                              ? static_cast<int32_t>(offset)
                              : 0;
      is_accessible = true;
      return true;
    }
    if (native_index <= 0 && !text->chunkNativeStart) {
      text->chunkOffset = 0;
      is_accessible = false;
      return true;
    }
  }
  return false;
}

UBool TextLatin1Access(UText* text, int64_t native_index, UBool forward) {
  if (!text->context) {
    return false;
  }
  int64_t native_length = TextNativeLength(text);
  UBool is_accessible;
  if (TextInChunkOrOutOfRange(text, native_index, native_length, forward, is_accessible)) {
    return is_accessible;
  }
  native_index = TextPinIndex(native_index, native_length - 1);
  TextContext current_context = TextLatin1GetCurrentContext(text);
  TextContext new_context = TextGetContext(text, native_index, forward);
  ;
  if (new_context == current_context) {
    if (current_context == kPrimaryContext) {
      TextLatin1MoveInPrimaryContext(text, native_index, native_length, forward);
    } else {
      TextLatin1MoveInPriorContext(text, native_index, native_length, forward);
    }
  } else if (new_context == kPrimaryContext) {
    TextLatin1SwitchToPrimaryContext(text, native_index, native_length, forward);
  } else {
    ;
    TextLatin1SwitchToPriorContext(text, native_index, native_length, forward);
  }
  return true;
}

constexpr struct UTextFuncs kTextLatin1Funcs = {
    sizeof(UTextFuncs),
    0,
    0,
    0,
    TextClone,
    TextNativeLength,
    TextLatin1Access,
    TextExtract,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    TextClose,
    nullptr,
    nullptr,
    nullptr,
};

void TextInit(UText* text,
              const UTextFuncs* funcs,
              const void* string,
              unsigned length,
              const UChar* prior_context,
              int prior_context_length) {
  text->pFuncs = funcs;
  text->providerProperties = 1 << UTEXT_PROVIDER_STABLE_CHUNKS;
  text->context = string;
  text->p = string;
  text->a = length;
  text->q = prior_context;
  text->b = prior_context_length;
}

UText* TextOpenLatin1(UTextWithBuffer* ut_with_buffer,
                      base::span<const LChar> string,
                      const UChar* prior_context,
                      int prior_context_length,
                      UErrorCode* status) {
  if (U_FAILURE(*status)) {
    return nullptr;
  }

  if (string.empty() || string.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
    *status = U_ILLEGAL_ARGUMENT_ERROR;
    return nullptr;
  }
  UText* text = utext_setup(&ut_with_buffer->text,
                            sizeof(ut_with_buffer->buffer),
                            status);
  if (U_FAILURE(*status)) {
    ;
    return nullptr;
  }
  TextInit(text, &kTextLatin1Funcs, string.data(), base::checked_cast<unsigned>(string.size()), prior_context, prior_context_length);
  return text;
}

inline TextContext TextUTF16GetCurrentContext(const UText* text) {
  if (!text->chunkContents) {
    return kNoContext;
  }
  return text->chunkContents == text->p ? kPrimaryContext : kPriorContext;
}

void TextUTF16MoveInPrimaryContext(UText* text,
                                   int64_t native_index,
                                   int64_t native_length,
                                   UBool forward) {
  ;
  ;
  ;
  text->chunkNativeStart = text->b;
  text->chunkNativeLimit = native_length;
  int64_t length = text->chunkNativeLimit - text->chunkNativeStart;
  // Ensure chunk length is well defined if computed length exceeds int32_t
  // range.
  ;
  text->chunkLength = length <= std::numeric_limits<int32_t>::max()
                          ? static_cast<int32_t>(length)
                          : 0;
  text->nativeIndexingLimit = text->chunkLength;
  int64_t offset = native_index - text->chunkNativeStart;
  // Ensure chunk offset is well defined if computed offset exceeds int32_t
  // range or chunk length.
  ;
  text->chunkOffset = std::min(offset <= std::numeric_limits<int32_t>::max()
                                   ? static_cast<int32_t>(offset)
                                   : 0,
                               text->chunkLength);
}

void TextUTF16SwitchToPrimaryContext(UText* text,
                                     int64_t native_index,
                                     int64_t native_length,
                                     UBool forward) {
  ;
  text->chunkContents = static_cast<const UChar*>(text->p);
  TextUTF16MoveInPrimaryContext(text, native_index, native_length, forward);
}

void TextUTF16MoveInPriorContext(UText* text,
                                 int64_t native_index,
                                 int64_t native_length,
                                 UBool forward) {
  ;
  ;
  ;
  ;
  text->chunkNativeStart = 0;
  text->chunkNativeLimit = text->b;
  text->chunkLength = text->b;
  text->nativeIndexingLimit = text->chunkLength;
  int64_t offset = native_index - text->chunkNativeStart;
  // Ensure chunk offset is well defined if computed offset exceeds
  // int32_t range or chunk length.
  ;
  text->chunkOffset = std::min(offset <= std::numeric_limits<int32_t>::max()
                                   ? static_cast<int32_t>(offset)
                                   : 0,
                               text->chunkLength);
}

void TextUTF16SwitchToPriorContext(UText* text,
                                   int64_t native_index,
                                   int64_t native_length,
                                   UBool forward) {
  ;
  text->chunkContents = static_cast<const UChar*>(text->q);
  TextUTF16MoveInPriorContext(text, native_index, native_length, forward);
}

UBool TextUTF16Access(UText* text, int64_t native_index, UBool forward) {
  if (!text->context) {
    return false;
  }
  int64_t native_length = TextNativeLength(text);
  UBool is_accessible;
  if (TextInChunkOrOutOfRange(text, native_index, native_length, forward, is_accessible)) {
    return is_accessible;
  }
  native_index = TextPinIndex(native_index, native_length - 1);
  TextContext current_context = TextUTF16GetCurrentContext(text);
  TextContext new_context = TextGetContext(text, native_index, forward);
  ;
  if (new_context == current_context) {
    if (current_context == kPrimaryContext) {
      TextUTF16MoveInPrimaryContext(text, native_index, native_length, forward);
    } else {
      TextUTF16MoveInPriorContext(text, native_index, native_length, forward);
    }
  } else if (new_context == kPrimaryContext) {
    TextUTF16SwitchToPrimaryContext(text, native_index, native_length, forward);
  } else {
    ;
    TextUTF16SwitchToPriorContext(text, native_index, native_length, forward);
  }
  return true;
}

constexpr struct UTextFuncs kTextUTF16Funcs = {
    sizeof(UTextFuncs),
    0,
    0,
    0,
    TextClone,
    TextNativeLength,
    TextUTF16Access,
    TextExtract,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    TextClose,
    nullptr,
    nullptr,
    nullptr,
};

UText* TextOpenUTF16(UText* text,
                     base::span<const UChar> string,
                     const UChar* prior_context,
                     int prior_context_length,
                     UErrorCode* status) {
  if (U_FAILURE(*status)) {
    return nullptr;
  }

  if (string.empty() || string.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
    *status = U_ILLEGAL_ARGUMENT_ERROR;
    return nullptr;
  }

  text = utext_setup(text, 0, status);
  if (U_FAILURE(*status)) {
    ;
    return nullptr;
  }
  TextInit(text, &kTextUTF16Funcs, string.data(), base::checked_cast<unsigned>(string.size()), prior_context, prior_context_length);
  return text;
}

constexpr UText g_empty_text = UTEXT_INITIALIZER;

TextBreakIterator* WordBreakIterator(base::span<const LChar> string) {
  UErrorCode error_code = U_ZERO_ERROR;
  static TextBreakIterator* break_iter = nullptr;
  if (!break_iter) {
    break_iter = icu::BreakIterator::createWordInstance(
        icu::Locale(CurrentTextBreakLocaleID()),
        error_code);
    ;
    if (!break_iter) {
      return nullptr;
    }
  }

  UTextWithBuffer text_local;
  text_local.text = g_empty_text;
  text_local.text.extraSize = sizeof(text_local.buffer);
  text_local.text.pExtra = text_local.buffer;

  UErrorCode open_status = U_ZERO_ERROR;
  UText* text = TextOpenLatin1(&text_local, string, nullptr, 0, &open_status);
  if (U_FAILURE(open_status)) {
    ;
    return nullptr;
  }

  UErrorCode set_text_status = U_ZERO_ERROR;
  break_iter->setText(text, set_text_status);
  if (U_FAILURE(set_text_status)) {
    ;
  }

  utext_close(text);

  return break_iter;
}

void SetText16(TextBreakIterator* iter, base::span<const UChar> string) {
  UErrorCode error_code = U_ZERO_ERROR;
  UText u_text = UTEXT_INITIALIZER;
  utext_openUChars(&u_text, string.data(), string.size(), &error_code);
  if (U_FAILURE(error_code)) {
    return;
  }
  iter->setText(&u_text, error_code);
}

} // namespace

TextBreakIterator* WordBreakIterator(base::span<const UChar> string) {
  UErrorCode error_code = U_ZERO_ERROR;
  static TextBreakIterator* break_iter = nullptr;
  if (!break_iter) {
    break_iter = icu::BreakIterator::createWordInstance(
        icu::Locale(CurrentTextBreakLocaleID()),
        error_code);
    ;
    if (!break_iter) {
      return nullptr;
    }
  }
  SetText16(break_iter, string);
  return break_iter;
}

TextBreakIterator* WordBreakIterator(const String& string,
                                     wtf_size_t start,
                                     wtf_size_t length) {
  if (string.empty()) {
    return nullptr;
  }
  if (string.Is8Bit()) {
    return WordBreakIterator(string.Span8().subspan(start, length));
  }
  return WordBreakIterator(string.Span16().subspan(start, length));
}

PooledBreakIterator AcquireLineBreakIterator(
    base::span<const LChar> string,
    const AtomicString& locale,
    const UChar* prior_context = nullptr,
    unsigned prior_context_length = 0) {
  PooledBreakIterator iterator{
      LineBreakIteratorPool::SharedPool().Take(locale)};
  if (!iterator) {
    return nullptr;
  }

  UTextWithBuffer text_local;
  text_local.text = g_empty_text;
  text_local.text.extraSize = sizeof(text_local.buffer);
  text_local.text.pExtra = text_local.buffer;

  UErrorCode open_status = U_ZERO_ERROR;
  UText* text = TextOpenLatin1(&text_local, string, prior_context, prior_context_length, &open_status);
  if (U_FAILURE(open_status)) {
    ;
    return nullptr;
  }

  UErrorCode set_text_status = U_ZERO_ERROR;
  iterator->setText(text, set_text_status);
  if (U_FAILURE(set_text_status)) {
    ;
    return nullptr;
  }

  utext_close(text);

  return iterator;
}

PooledBreakIterator AcquireLineBreakIterator(
    base::span<const UChar> string,
    const AtomicString& locale,
    const UChar* prior_context = nullptr,
    unsigned prior_context_length = 0) {
  PooledBreakIterator iterator{
      LineBreakIteratorPool::SharedPool().Take(locale)};
  if (!iterator) {
    return nullptr;
  }

  UText text_local = UTEXT_INITIALIZER;

  UErrorCode open_status = U_ZERO_ERROR;
  UText* text = TextOpenUTF16(&text_local, string, prior_context, prior_context_length, &open_status);
  if (U_FAILURE(open_status)) {
    ;
    return nullptr;
  }

  UErrorCode set_text_status = U_ZERO_ERROR;
  iterator->setText(text, set_text_status);
  if (U_FAILURE(set_text_status)) {
    ;
    return nullptr;
  }

  utext_close(text);

  return iterator;
}

PooledBreakIterator AcquireLineBreakIterator(StringView string,
                                             const AtomicString& locale) {
  if (string.Is8Bit()) {
    return AcquireLineBreakIterator(string.Span8(), locale);
  }
  return AcquireLineBreakIterator(string.Span16(), locale);
}

void ReturnBreakIteratorToPool::operator()(void* ptr) const {
  TextBreakIterator* iterator = static_cast<TextBreakIterator*>(ptr);
  ;
  LineBreakIteratorPool::SharedPool().Put(iterator);
}

//
// A simple pool of `icu::BreakIterator` without any keys, as
// `CharacterBreakIterator` is locale-independent.
//
class CharacterBreakIterator::Pool {
public:
  static Pool& Get() {
    DEFINE_THREAD_SAFE_STATIC_LOCAL(ThreadSpecific<Pool>, pool, ());
    return *pool;
  }

  PooledIterator TakeOrCreate() {
    if (!pool_.empty()) {
      PooledIterator iterator(pool_.back().release());
      pool_.pop_back();
      return iterator;
    }

    ICUError error_code;
    PooledIterator iterator(icu::BreakIterator::createCharacterInstance(
        icu::Locale(CurrentTextBreakLocaleID()),
        error_code));
    ;
    return iterator;
  }

  void Put(icu::BreakIterator* iterator) {
    pool_.push_back(iterator);
  }

private:
  static constexpr size_t kCapacity = 4;
  Vector<std::unique_ptr<icu::BreakIterator>, kCapacity> pool_;
};

void CharacterBreakIterator::ReturnToPool::operator()(void* ptr) const {
  icu::BreakIterator* iterator = static_cast<icu::BreakIterator*>(ptr);
  ;
  Pool::Get().Put(iterator);
}

CharacterBreakIterator::CharacterBreakIterator(const StringView& string) {
  if (string.empty()) {
    is_8bit_ = true;
    return;
  }

  is_8bit_ = string.Is8Bit();

  if (is_8bit_) {
    base::span<const LChar> chars = string.Span8();
    charaters8_ = chars.data();
    offset_ = 0;
    // static_cast<> is safe because `chars` came from a StringView.
    length_ = static_cast<unsigned>(chars.size());
    return;
  }

  CreateIteratorForBuffer(string.Span16());
}

CharacterBreakIterator::CharacterBreakIterator(base::span<const UChar> buffer) {
  CreateIteratorForBuffer(buffer);
}

void CharacterBreakIterator::CreateIteratorForBuffer(
    base::span<const UChar> buffer) {
  iterator_ = Pool::Get().TakeOrCreate();
  SetText16(iterator_.get(), buffer);
}

int CharacterBreakIterator::Next() {
  if (!is_8bit_) {
    return iterator_->next();
  }

  if (offset_ >= length_) {
    return kTextBreakDone;
  }

  offset_ += ClusterLengthStartingAt(offset_);
  return offset_;
}

int CharacterBreakIterator::Current() {
  if (!is_8bit_) {
    return iterator_->current();
  }
  return offset_;
}

bool CharacterBreakIterator::IsBreak(int offset) const {
  if (!is_8bit_) {
    return iterator_->isBoundary(offset);
  }
  return !IsLFAfterCR(offset);
}

int CharacterBreakIterator::Preceding(int offset) const {
  if (!is_8bit_) {
    return iterator_->preceding(offset);
  }
  if (offset <= 0) {
    return kTextBreakDone;
  }
  if (IsLFAfterCR(offset)) {
    return offset - 2;
  }
  return offset - 1;
}

int CharacterBreakIterator::Following(int offset) const {
  if (!is_8bit_) {
    return iterator_->following(offset);
  }
  if (static_cast<unsigned>(offset) >= length_) {
    return kTextBreakDone;
  }
  return offset + ClusterLengthStartingAt(offset);
}

TextBreakIterator* SentenceBreakIterator(base::span<const UChar> string) {
  UErrorCode open_status = U_ZERO_ERROR;
  static TextBreakIterator* iterator = nullptr;
  if (!iterator) {
    iterator = icu::BreakIterator::createSentenceInstance(
        icu::Locale(CurrentTextBreakLocaleID()),
        open_status);
    ;
    if (!iterator) {
      return nullptr;
    }
  }

  SetText16(iterator, string);
  return iterator;
}

bool IsWordTextBreak(TextBreakIterator* iterator) {
  icu::RuleBasedBreakIterator* rule_based_break_iterator =
      static_cast<icu::RuleBasedBreakIterator*>(iterator);
  int rule_status = rule_based_break_iterator->getRuleStatus();
  return rule_status != UBRK_WORD_NONE;
}

TextBreakIterator* CursorMovementIterator(base::span<const UChar> string) {
  // This rule set is based on character-break iterator rules of ICU 4.0
  // <http://source.icu-project.org/repos/icu/icu/tags/release-4-0/source/data/brkitr/char.txt>.
  // The major differences from the original ones are listed below:
  // * Replaced '[\p{Grapheme_Cluster_Break = SpacingMark}]' with
  //   '[\p{General_Category = Spacing Mark} - $Extend]' for ICU 3.8 or earlier;
  // * Removed rules that prevent a cursor from moving after prepend characters
  //   (Bug 24342);
  // * Added rules that prevent a cursor from moving after virama signs of Indic
  //   languages except Tamil (Bug 15790), and;
  // * Added rules that prevent a cursor from moving before Japanese half-width
  //   katakara voiced marks.
  // * Added rules for regional indicator symbols.
  static const char* const kRules =
      "$CR      = [\\p{Grapheme_Cluster_Break = CR}];"
      "$LF      = [\\p{Grapheme_Cluster_Break = LF}];"
      "$Control = [\\p{Grapheme_Cluster_Break = Control}];"
      "$VoiceMarks = [\\uFF9E\\uFF9F];" // Japanese half-width katakana voiced
                                        // marks
      "$Extend  = [\\p{Grapheme_Cluster_Break = Extend} $VoiceMarks - [\\u0E30 "
      "\\u0E32 \\u0E45 \\u0EB0 \\u0EB2]];"
      "$SpacingMark = [[\\p{General_Category = Spacing Mark}] - $Extend];"
      "$L       = [\\p{Grapheme_Cluster_Break = L}];"
      "$V       = [\\p{Grapheme_Cluster_Break = V}];"
      "$T       = [\\p{Grapheme_Cluster_Break = T}];"
      "$LV      = [\\p{Grapheme_Cluster_Break = LV}];"
      "$LVT     = [\\p{Grapheme_Cluster_Break = LVT}];"
      "$Hin0    = [\\u0905-\\u0939];"         // Devanagari Letter A,...,Ha
      "$HinV    = \\u094D;"                   // Devanagari Sign Virama
      "$Hin1    = [\\u0915-\\u0939];"         // Devanagari Letter Ka,...,Ha
      "$Ben0    = [\\u0985-\\u09B9];"         // Bengali Letter A,...,Ha
      "$BenV    = \\u09CD;"                   // Bengali Sign Virama
      "$Ben1    = [\\u0995-\\u09B9];"         // Bengali Letter Ka,...,Ha
      "$Pan0    = [\\u0A05-\\u0A39];"         // Gurmukhi Letter A,...,Ha
      "$PanV    = \\u0A4D;"                   // Gurmukhi Sign Virama
      "$Pan1    = [\\u0A15-\\u0A39];"         // Gurmukhi Letter Ka,...,Ha
      "$Guj0    = [\\u0A85-\\u0AB9];"         // Gujarati Letter A,...,Ha
      "$GujV    = \\u0ACD;"                   // Gujarati Sign Virama
      "$Guj1    = [\\u0A95-\\u0AB9];"         // Gujarati Letter Ka,...,Ha
      "$Ori0    = [\\u0B05-\\u0B39];"         // Oriya Letter A,...,Ha
      "$OriV    = \\u0B4D;"                   // Oriya Sign Virama
      "$Ori1    = [\\u0B15-\\u0B39];"         // Oriya Letter Ka,...,Ha
      "$Tel0    = [\\u0C05-\\u0C39];"         // Telugu Letter A,...,Ha
      "$TelV    = \\u0C4D;"                   // Telugu Sign Virama
      "$Tel1    = [\\u0C14-\\u0C39];"         // Telugu Letter Ka,...,Ha
      "$Kan0    = [\\u0C85-\\u0CB9];"         // Kannada Letter A,...,Ha
      "$KanV    = \\u0CCD;"                   // Kannada Sign Virama
      "$Kan1    = [\\u0C95-\\u0CB9];"         // Kannada Letter A,...,Ha
      "$Mal0    = [\\u0D05-\\u0D39];"         // Malayalam Letter A,...,Ha
      "$MalV    = \\u0D4D;"                   // Malayalam Sign Virama
      "$Mal1    = [\\u0D15-\\u0D39];"         // Malayalam Letter A,...,Ha
      "$RI      = [\\U0001F1E6-\\U0001F1FF];" // Emoji regional indicators
      "!!chain;"
      "!!forward;"
      "$CR $LF;"
      "$L ($L | $V | $LV | $LVT);"
      "($LV | $V) ($V | $T);"
      "($LVT | $T) $T;"
      "[^$Control $CR $LF] $Extend;"
      "[^$Control $CR $LF] $SpacingMark;"
      "$RI $RI / $RI;"
      "$RI $RI;"
      "$Hin0 $HinV $Hin1;" // Devanagari Virama (forward)
      "$Ben0 $BenV $Ben1;" // Bengali Virama (forward)
      "$Pan0 $PanV $Pan1;" // Gurmukhi Virama (forward)
      "$Guj0 $GujV $Guj1;" // Gujarati Virama (forward)
      "$Ori0 $OriV $Ori1;" // Oriya Virama (forward)
      "$Tel0 $TelV $Tel1;" // Telugu Virama (forward)
      "$Kan0 $KanV $Kan1;" // Kannada Virama (forward)
      "$Mal0 $MalV $Mal1;" // Malayalam Virama (forward)
      "!!reverse;"
      "$LF $CR;"
      "($L | $V | $LV | $LVT) $L;"
      "($V | $T) ($LV | $V);"
      "$T ($LVT | $T);"
      "$Extend      [^$Control $CR $LF];"
      "$SpacingMark [^$Control $CR $LF];"
      "$RI $RI / $RI $RI;"
      "$RI $RI;"
      "$Hin1 $HinV $Hin0;" // Devanagari Virama (backward)
      "$Ben1 $BenV $Ben0;" // Bengali Virama (backward)
      "$Pan1 $PanV $Pan0;" // Gurmukhi Virama (backward)
      "$Guj1 $GujV $Guj0;" // Gujarati Virama (backward)
      "$Ori1 $OriV $Ori0;" // Gujarati Virama (backward)
      "$Tel1 $TelV $Tel0;" // Telugu Virama (backward)
      "$Kan1 $KanV $Kan0;" // Kannada Virama (backward)
      "$Mal1 $MalV $Mal0;" // Malayalam Virama (backward)
      "!!safe_reverse;"
      "!!safe_forward;";

  if (string.empty()) {
    return nullptr;
  }

  DEFINE_THREAD_SAFE_STATIC_LOCAL(
      ThreadSpecific<std::unique_ptr<icu::RuleBasedBreakIterator>>,
      thread_specific,
      ());

  std::unique_ptr<icu::RuleBasedBreakIterator>& iterator = *thread_specific;

  if (!iterator) {
    UParseError parse_status;
    UErrorCode open_status = U_ZERO_ERROR;
    // break_rules is ASCII. Pick the most efficient UnicodeString ctor.
    iterator = std::make_unique<icu::RuleBasedBreakIterator>(
        icu::UnicodeString(kRules, -1, US_INV),
        parse_status,
        open_status);
    ;
  }

  SetText16(iterator.get(), string);
  return iterator.get();
}

} // namespace blink
