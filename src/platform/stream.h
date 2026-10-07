// Ported from: skia/include/core/SkStream.h

#pragma once

#include <cstddef>
#include <memory>

#include "base/text/wtf_string.h"
#include "data.h"

namespace bkit {

class StreamAsset;

// SkStream. SkStreamRewindable and SkStreamSeekable are folded in; their
// capabilities are reported through HasPosition and HasLength as upstream.
class Stream {
public:
  virtual ~Stream() = default;
  Stream() = default;
  Stream(const Stream&) = delete;
  Stream& operator=(const Stream&) = delete;

  // Reads or skips size number of bytes. If buffer is null, skip size bytes,
  // return how many were skipped. If buffer is not null, copy size bytes into
  // buffer, return how many were copied.
  virtual std::size_t Read(void* buffer, std::size_t size) = 0;

  // Returns true when all the bytes in the stream have been read.
  virtual bool IsAtEnd() const = 0;

  // Rewinds to the beginning of the stream. Returns true if the stream is
  // known to be at the beginning after this call returns.
  virtual bool Rewind() {
    return false;
  }

  // Returns true if this stream can report its current position.
  virtual bool HasPosition() const {
    return false;
  }
  // Returns the current position in the stream. If this cannot be done,
  // returns 0.
  virtual std::size_t GetPosition() const {
    return 0;
  }

  // Seeks to an absolute position in the stream. If this cannot be done,
  // returns false. If an attempt is made to seek past the end of the stream,
  // the position will be set to the end of the stream.
  virtual bool Seek(std::size_t) {
    return false;
  }

  // Seeks to an relative offset in the stream. If this cannot be done,
  // returns false.
  virtual bool Move(long) {
    return false;
  }

  // Returns true if this stream can report its total length.
  virtual bool HasLength() const {
    return false;
  }
  // Returns the total length of the stream. If this cannot be done, returns 0.
  virtual std::size_t GetLength() const {
    return 0;
  }

  // Returns the starting address for the data. If this cannot be done,
  // returns null.
  virtual const void* GetMemoryBase() {
    return nullptr;
  }

  // Attempts to open the specified file as a stream, returns null on failure.
  static std::unique_ptr<StreamAsset> MakeFromFile(const String& path);
};

// SkStreamAsset: a seekable stream with a known length.
class StreamAsset : public Stream {
public:
  bool HasLength() const override {
    return true;
  }
  bool HasPosition() const override {
    return true;
  }

  // Duplicates this stream. If this cannot be done, returns null. The
  // returned stream will be positioned at the beginning of its data.
  std::unique_ptr<StreamAsset> Duplicate() const {
    return std::unique_ptr<StreamAsset>(OnDuplicate());
  }
  // Duplicates this stream. If this cannot be done, returns null. The
  // returned stream will be positioned the same as this stream.
  std::unique_ptr<StreamAsset> Fork() const {
    return std::unique_ptr<StreamAsset>(OnFork());
  }

private:
  virtual StreamAsset* OnDuplicate() const = 0;
  virtual StreamAsset* OnFork() const = 0;
};

// SkMemoryStream.
class MemoryStream final : public StreamAsset {
public:
  explicit MemoryStream(std::shared_ptr<Data> data);

  // Returns a stream with a copy of the input data.
  static std::unique_ptr<MemoryStream> MakeCopy(const void* data, std::size_t length);

  // Returns a stream with a shared reference to the input data.
  static std::unique_ptr<MemoryStream> Make(std::shared_ptr<Data> data);

  std::shared_ptr<Data> GetData() const {
    return data_;
  }

  std::size_t Read(void* buffer, std::size_t size) override;
  bool IsAtEnd() const override;

  bool Rewind() override;

  std::size_t GetPosition() const override;
  bool Seek(std::size_t position) override;
  bool Move(long offset) override;

  std::size_t GetLength() const override;

  const void* GetMemoryBase() override;

private:
  MemoryStream* OnDuplicate() const override;
  MemoryStream* OnFork() const override;

  std::shared_ptr<Data> data_;
  std::size_t offset_;
};

} // namespace bkit
