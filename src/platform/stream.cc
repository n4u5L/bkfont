// Ported from: skia/src/core/SkStream.cpp

#include "stream.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace bkit {

MemoryStream::MemoryStream(std::shared_ptr<Data> data)
    : data_(std::move(data)) {
  if (nullptr == data_) {
    data_ = Data::MakeEmpty();
  }
  offset_ = 0;
}

std::unique_ptr<MemoryStream> MemoryStream::MakeCopy(const void* data, std::size_t length) {
  return std::make_unique<MemoryStream>(Data::MakeWithCopy(data, length));
}

std::unique_ptr<MemoryStream> MemoryStream::Make(std::shared_ptr<Data> data) {
  return std::make_unique<MemoryStream>(std::move(data));
}

std::size_t MemoryStream::Read(void* buffer, std::size_t size) {
  std::size_t data_size = data_->size();

  size = std::min(size, data_size - offset_);
  if (buffer) {
    std::memcpy(buffer, data_->bytes() + offset_, size);
  }
  offset_ += size;
  return size;
}

bool MemoryStream::IsAtEnd() const {
  return offset_ == data_->size();
}

bool MemoryStream::Rewind() {
  offset_ = 0;
  return true;
}

MemoryStream* MemoryStream::OnDuplicate() const {
  return new MemoryStream(data_);
}

std::size_t MemoryStream::GetPosition() const {
  return offset_;
}

bool MemoryStream::Seek(std::size_t position) {
  offset_ = position > data_->size() ? data_->size() : position;
  return true;
}

bool MemoryStream::Move(long offset) {
  return Seek(offset_ + offset);
}

MemoryStream* MemoryStream::OnFork() const {
  std::unique_ptr<MemoryStream> that(OnDuplicate());
  that->Seek(offset_);
  return that.release();
}

std::size_t MemoryStream::GetLength() const {
  return data_->size();
}

const void* MemoryStream::GetMemoryBase() {
  return data_->data();
}

// SkStream::MakeFromFile. Data::MakeFromFileName reads the whole file, so the
// SkFILEStream fallback for a failed mmap has nothing left to try.
std::unique_ptr<StreamAsset> Stream::MakeFromFile(const String& path) {
  std::shared_ptr<Data> data(Data::MakeFromFileName(path));
  if (data) {
    return std::make_unique<MemoryStream>(std::move(data));
  }
  return nullptr;
}

} // namespace bkit
