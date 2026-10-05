// Ported from: skia/src/utils/win/SkDWriteFontFileStream.cpp

#include "dwrite_font_file_stream.h"

#include <cstring>
#include <limits>
#include <memory>

namespace bkfont {

DWriteFontFileStream::DWriteFontFileStream(IDWriteFontFileStream* font_file_stream)
    : font_file_stream_(font_file_stream),
      pos_(0),
      locked_memory_(nullptr),
      fragment_lock_(nullptr) {
}

DWriteFontFileStream::~DWriteFontFileStream() {
  if (fragment_lock_) {
    font_file_stream_->ReleaseFileFragment(fragment_lock_);
  }
}

std::size_t DWriteFontFileStream::Read(void* buffer, std::size_t size) {
  HRESULT hr = S_OK;

  if (nullptr == buffer) {
    std::size_t file_size = GetLength();

    if (pos_ + size > file_size) {
      std::size_t skipped = file_size - pos_;
      pos_ = file_size;
      return skipped;
    } else {
      pos_ += size;
      return size;
    }
  }

  const void* start;
  void* fragment_lock;
  hr = font_file_stream_->ReadFileFragment(&start, pos_, size, &fragment_lock);
  if (SUCCEEDED(hr)) {
    std::memcpy(buffer, start, size);
    font_file_stream_->ReleaseFileFragment(fragment_lock);
    pos_ += size;
    return size;
  }

  // The read may have failed because we asked for too much data.
  std::size_t file_size = GetLength();
  if (pos_ + size <= file_size) {
    // This means we were within bounds, but failed for some other reason.
    return 0;
  }

  std::size_t read = file_size - pos_;
  hr = font_file_stream_->ReadFileFragment(&start, pos_, read, &fragment_lock);
  if (SUCCEEDED(hr)) {
    std::memcpy(buffer, start, read);
    font_file_stream_->ReleaseFileFragment(fragment_lock);
    pos_ = file_size;
    return read;
  }

  return 0;
}

bool DWriteFontFileStream::IsAtEnd() const {
  return pos_ == GetLength();
}

bool DWriteFontFileStream::Rewind() {
  pos_ = 0;
  return true;
}

DWriteFontFileStream* DWriteFontFileStream::OnDuplicate() const {
  return new DWriteFontFileStream(font_file_stream_.Get());
}

std::size_t DWriteFontFileStream::GetPosition() const {
  return pos_;
}

bool DWriteFontFileStream::Seek(std::size_t position) {
  std::size_t length = GetLength();
  pos_ = (position > length) ? length : position;
  return true;
}

bool DWriteFontFileStream::Move(long offset) {
  return Seek(pos_ + offset);
}

DWriteFontFileStream* DWriteFontFileStream::OnFork() const {
  std::unique_ptr<DWriteFontFileStream> that(OnDuplicate());
  that->Seek(pos_);
  return that.release();
}

std::size_t DWriteFontFileStream::GetLength() const {
  UINT64 real_file_size = 0;
  font_file_stream_->GetFileSize(&real_file_size);
  if (real_file_size > std::numeric_limits<std::size_t>::max()) {
    return 0;
  }
  return static_cast<std::size_t>(real_file_size);
}

const void* DWriteFontFileStream::GetMemoryBase() {
  if (locked_memory_) {
    return locked_memory_;
  }

  UINT64 file_size;
  if (FAILED(font_file_stream_->GetFileSize(&file_size))) {
    return nullptr;
  }
  if (FAILED(font_file_stream_->ReadFileFragment(&locked_memory_, 0, file_size, &fragment_lock_))) {
    return nullptr;
  }
  return locked_memory_;
}

} // namespace bkfont
