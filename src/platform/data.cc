// Ported from: skia/src/core/SkData.cpp

#include "data.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "stream.h"

namespace bkit {

namespace {

// SkStreamPriv::StreamRemainingLengthIsBelow.
bool StreamRemainingLengthIsBelow(Stream* stream, std::size_t len) {
  if (stream->HasLength() && stream->HasPosition()) {
    std::size_t remaining_bytes = stream->GetLength() - stream->GetPosition();
    return len > remaining_bytes;
  }
  return false;
}

} // namespace

Data::Data(std::size_t length)
    : bytes_(length ? std::make_unique<std::uint8_t[]>(length) : nullptr),
      size_(length) {
}

Data::~Data() = default;

std::shared_ptr<Data> Data::PrivateNewWithCopy(const void* src_or_null, std::size_t length) {
  if (0 == length) {
    return Data::MakeEmpty();
  }

  std::shared_ptr<Data> data(new Data(length));
  if (src_or_null) {
    std::memcpy(data->writable_data(), src_or_null, length);
  }
  return data;
}

std::shared_ptr<Data> Data::MakeEmpty() {
  static const std::shared_ptr<Data>& empty = *new std::shared_ptr<Data>(new Data(0));
  return empty;
}

std::shared_ptr<Data> Data::MakeWithCopy(const void* src, std::size_t length) {
  return PrivateNewWithCopy(src, length);
}

std::shared_ptr<Data> Data::MakeUninitialized(std::size_t length) {
  return PrivateNewWithCopy(nullptr, length);
}

std::shared_ptr<Data> Data::MakeFromFileName(const String& path) {
  if (path.IsNull()) {
    return nullptr;
  }
  const std::string utf8 = path.Utf8();
  const std::filesystem::path file_path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
  std::ifstream file(file_path, std::ios::binary);
  if (!file) {
    return nullptr;
  }
  file.seekg(0, std::ios::end);
  const std::streamoff length = file.tellg();
  if (length < 0) {
    return nullptr;
  }
  file.seekg(0, std::ios::beg);
  std::shared_ptr<Data> data = MakeUninitialized(static_cast<std::size_t>(length));
  if (length && !file.read(static_cast<char*>(data->writable_data()), length)) {
    return nullptr;
  }
  return data;
}

std::shared_ptr<Data> Data::MakeFromStream(Stream* stream, std::size_t size) {
  // reduce the chance of OOM by checking that the stream has enough bytes to
  // read from before allocating that potentially large buffer.
  if (StreamRemainingLengthIsBelow(stream, size)) {
    return nullptr;
  }
  std::shared_ptr<Data> data(Data::MakeUninitialized(size));
  if (stream->Read(data->writable_data(), size) != size) {
    return nullptr;
  }
  return data;
}

} // namespace bkit
