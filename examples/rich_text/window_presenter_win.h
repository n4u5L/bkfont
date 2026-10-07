#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "base/lean_windows.h"
#include "paint/pixmap.h"

namespace rich_text {

// UI-thread presentation of the CPU raster. DirectComposition retains the
// submitted pixels; ordinary HWND exposure needs no upload or new frame.
class WindowPresenter {
public:
  struct Scroll {
    bkfont::IntRect area;
    int dx = 0, dy = 0;
  };
  struct Statistics {
    uint64_t upload_bytes = 0, uploads = 0, scrolls = 0, commits = 0, retained = 0, transforms = 0;
  };
  WindowPresenter();
  ~WindowPresenter();
  WindowPresenter(const WindowPresenter&) = delete;
  WindowPresenter& operator=(const WindowPresenter&) = delete;

  void Initialize(HWND window, bool force_gdi);
  // Call on every WM_PAINT, even without damage, to detect device loss.
  // False means the caller must paint its retained bitmap using GDI.
  // preview_scale animates retained pixels about the window center, without
  // relayout. Normal editing uses 1; layout zoom remains the editor's job.
  bool Present(const bkfont::Pixmap&, std::span<const bkfont::IntRect> damage, bool full, const Scroll* = nullptr,
               float preview_scale = 1);
  bool IsComposed() const;
  Statistics GetStatistics() const;
  bool TakeGdiRepaint();
  void ReportMemory() const;
  // Release the composition target before destroying its HWND.
  void Shutdown();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace rich_text
