// Ported from: skia/src/utils/win/SkDWriteFontFileStream.h

#pragma once

#include <cstddef>

#include "dwrite_internal.h"
#include "stream.h"

namespace bkfont {

// SkDWriteFontFileStream. An SkStream backed by an IDWriteFontFileStream. This
// allows Skia code to read an IDWriteFontFileStream.
class DWriteFontFileStream final : public StreamAsset {
public:
  explicit DWriteFontFileStream(IDWriteFontFileStream* font_file_stream);
  ~DWriteFontFileStream() override;

  std::size_t Read(void* buffer, std::size_t size) override;
  bool IsAtEnd() const override;
  bool Rewind() override;
  std::size_t GetPosition() const override;
  bool Seek(std::size_t position) override;
  bool Move(long offset) override;
  std::size_t GetLength() const override;
  const void* GetMemoryBase() override;

private:
  DWriteFontFileStream* OnDuplicate() const override;
  DWriteFontFileStream* OnFork() const override;

  ComPtr<IDWriteFontFileStream> font_file_stream_;
  std::size_t pos_;
  const void* locked_memory_;
  void* fragment_lock_;
};

} // namespace bkfont
