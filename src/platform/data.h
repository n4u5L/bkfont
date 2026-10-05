// Ported from: skia/include/core/SkData.h

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "base/text/wtf_string.h"

namespace bkfont {

class Stream;

// SkData. Shared ownership stands in for sk_sp.
class Data final {
public:
  ~Data();
  Data(const Data&) = delete;
  Data& operator=(const Data&) = delete;

  std::size_t size() const {
    return size_;
  }
  bool empty() const {
    return 0 == size_;
  }
  const void* data() const {
    return bytes_.get();
  }
  const std::uint8_t* bytes() const {
    return bytes_.get();
  }

  // USE WITH CAUTION. Only for freshly made data that nobody else references.
  void* writable_data() {
    return bytes_.get();
  }

  // Create a new dataref by copying the specified data.
  static std::shared_ptr<Data> MakeWithCopy(const void* data, std::size_t length);

  // Create a new data with uninitialized contents.
  static std::shared_ptr<Data> MakeUninitialized(std::size_t length);

  // Create a new dataref from a file, or return null if the file cannot be
  // opened. The upstream implementation memory-maps the file; this one reads
  // it, so the bytes are the same.
  static std::shared_ptr<Data> MakeFromFileName(const String& path);

  // Attempt to read size bytes into a Data. If the read succeeds, return the
  // data, else return null. Either way the stream's cursor may have been
  // changed as a result of calling read().
  static std::shared_ptr<Data> MakeFromStream(Stream* stream, std::size_t size);

  // Returns a new empty dataref (or a reference to a shared empty dataref).
  static std::shared_ptr<Data> MakeEmpty();

private:
  explicit Data(std::size_t length);

  static std::shared_ptr<Data> PrivateNewWithCopy(const void* src_or_null, std::size_t length);

  std::unique_ptr<std::uint8_t[]> bytes_;
  std::size_t size_;
};

} // namespace bkfont
