#include "window_presenter_win.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

namespace rich_text {
namespace {
using Microsoft::WRL::ComPtr;
using bkit::IntRect;
using bkit::Pixmap;
} // namespace

struct WindowPresenter::Impl {
  HWND window = nullptr;
  bool owns_com = false, gdi_repaint = false;
  int width = 0, height = 0;
  float preview_scale = 1;
  HRESULT failure = S_OK;
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  ComPtr<IDXGIAdapter3> adapter;
  ComPtr<IDCompositionDevice> composition;
  ComPtr<IDCompositionTarget> target;
  ComPtr<IDCompositionVisual> visual;
  ComPtr<IDCompositionVirtualSurface> surface;
  // A bounded ring instead of window-sized driver-owned upload copies.
  static constexpr int kUploadTile = 512;
  std::array<ComPtr<ID3D11Texture2D>, 2> uploads;
  unsigned next_upload = 0;
  Statistics statistics;

  void ReleaseDevice() {
    if (target) {
      target->SetRoot(nullptr);
      if (composition) composition->Commit();
    }
    surface.Reset();
    for (auto& upload : uploads) upload.Reset();
    next_upload = 0;
    visual.Reset();
    target.Reset();
    composition.Reset();
    adapter.Reset();
    context.Reset();
    device.Reset();
    width = height = 0;
    preview_scale = 1;
  }

  HRESULT CreateDevice() {
    // This is a CPU renderer already: if hardware composition is unavailable,
    // use GDI rather than adding a second software renderer through WARP.
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                     nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf());
    if (FAILED(hr)) return hr;
    ComPtr<IDXGIDevice> dxgi;
    hr = device.As(&dxgi);
    if (FAILED(hr)) return hr;
    ComPtr<IDXGIAdapter> base_adapter;
    if (SUCCEEDED(dxgi->GetAdapter(base_adapter.GetAddressOf()))) base_adapter.As(&adapter);
    hr = DCompositionCreateDevice(dxgi.Get(), __uuidof(IDCompositionDevice),
                                    reinterpret_cast<void**>(composition.GetAddressOf()));
    if (FAILED(hr)) return hr;
    hr = composition->CreateTargetForHwnd(window, TRUE, target.GetAddressOf());
    if (FAILED(hr)) return hr;
    hr = composition->CreateVisual(visual.GetAddressOf());
    if (FAILED(hr)) return hr;
    // A virtual surface can resize in place and discard storage outside the
    // new bounds. It does not require an application-owned swap chain or a
    // second CPU bitmap. Allocate pixels only when the first frame arrives.
    hr = composition->CreateVirtualSurface(1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_IGNORE, surface.GetAddressOf());
    if (FAILED(hr)) return hr;
    hr = visual->SetContent(surface.Get());
    if (FAILED(hr)) return hr;
    return target->SetRoot(visual.Get());
  }

  void UseGdi(HRESULT hr) {
    failure = hr;
    ReleaseDevice();
    gdi_repaint = true; // Removing the visual exposes the whole HWND bitmap.
  }

  HRESULT CopyPixels(const Pixmap& pixels, IntRect area, ID3D11Texture2D* texture, UINT subresource, POINT offset) {
    if (static_cast<size_t>(area.Width()) * area.Height() <= kUploadTile * kUploadTile) {
      const D3D11_BOX box{static_cast<UINT>(offset.x), static_cast<UINT>(offset.y), 0,
                            static_cast<UINT>(offset.x + area.Width()), static_cast<UINT>(offset.y + area.Height()), 1};
      context->UpdateSubresource(texture, subresource, &box, pixels.WritableAddr8(area.left, area.top),
                                    static_cast<UINT>(pixels.RowBytes()), 0);
      return S_OK;
    }
    for (int top = area.top; top < area.bottom; top += kUploadTile) {
      for (int left = area.left; left < area.right; left += kUploadTile) {
        auto& upload = uploads[next_upload];
        next_upload = (next_upload + 1) % uploads.size();
        if (!upload) {
          D3D11_TEXTURE2D_DESC desc{};
          desc.Width = desc.Height = kUploadTile;
          desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
          desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
          desc.Usage = D3D11_USAGE_STAGING;
          desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
          const HRESULT hr = device->CreateTexture2D(&desc, nullptr, upload.GetAddressOf());
          if (FAILED(hr)) return hr;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        // WRITE waits for this slot's previous GPU copy before reuse. DISCARD
        // would allow the driver to grow an unbounded ring under resize/load.
        const HRESULT hr = context->Map(upload.Get(), 0, D3D11_MAP_WRITE, 0, &mapped);
        if (FAILED(hr)) return hr;
        const int width = std::min(kUploadTile, area.right - left), height = std::min(kUploadTile, area.bottom - top);
        for (int row = 0; row < height; ++row)
          std::memcpy(static_cast<unsigned char*>(mapped.pData) + static_cast<size_t>(row) * mapped.RowPitch,
                      pixels.WritableAddr8(left, top + row), static_cast<size_t>(width) * 4);
        context->Unmap(upload.Get(), 0);
        const D3D11_BOX box{0, 0, 0, static_cast<UINT>(width), static_cast<UINT>(height), 1};
        context->CopySubresourceRegion(texture, subresource, offset.x + left - area.left, offset.y + top - area.top, 0,
                                       upload.Get(), 0, &box);
      }
    }
    return S_OK;
  }

  HRESULT Upload(const Pixmap& pixels, IntRect area) {
    if (area.IsEmpty()) return S_OK;
    const RECT rect{area.left, area.top, area.right, area.bottom};
    POINT offset{};
    ComPtr<IDXGISurface> update;
    HRESULT hr = surface->BeginDraw(&rect, __uuidof(IDXGISurface), reinterpret_cast<void**>(update.GetAddressOf()), &offset);
    if (FAILED(hr)) return hr;
    // BeginDraw can return a different atlas allocation/subresource on each
    // update. Its origin is NOT necessarily rect.left/top. Copy every pixel
    // in the requested rectangle: previous contents are undefined here.
    ComPtr<IDXGISurface2> resource_surface;
    ComPtr<ID3D11Texture2D> texture;
    UINT subresource = 0;
    hr = update.As(&resource_surface);
    if (SUCCEEDED(hr))
      hr = resource_surface->GetResource(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(texture.GetAddressOf()), &subresource);
    if (SUCCEEDED(hr)) {
      hr = CopyPixels(pixels, area, texture.Get(), subresource, offset);
      // Submit commands to the shared resource before returning it to the
      // compositor. Flush submits asynchronously; it does not wait for GPU.
      context->Flush();
      if (SUCCEEDED(hr)) hr = device->GetDeviceRemovedReason();
    }
    const HRESULT ended = surface->EndDraw(); // Balance BeginDraw on failure too.
    if (SUCCEEDED(hr) && SUCCEEDED(ended)) {
      ++statistics.uploads;
      statistics.upload_bytes += static_cast<uint64_t>(area.Width()) * area.Height() * 4;
    }
    return FAILED(hr) ? hr : ended;
  }

  HRESULT Submit(const Pixmap& pixels, std::span<const IntRect> damage, bool full, const Scroll* scroll, float scale) {
    if (pixels.GetColorType() != bkit::ColorType::kN32 || pixels.RowBytes() > std::numeric_limits<UINT>::max())
      return E_INVALIDARG;
    if (width != pixels.Width() || height != pixels.Height()) {
      const HRESULT hr = surface->Resize(pixels.Width(), pixels.Height());
      if (FAILED(hr)) return hr;
      width = pixels.Width();
      height = pixels.Height();
      full = true;
    }
    const bool transformed = preview_scale != scale;
    if (!full && damage.empty() && !scroll && !transformed) {
      ++statistics.retained;
      return S_OK; // DWM retains the existing frame.
    }
    if (transformed || (full && scale != 1)) {
      const int scaled_width = static_cast<int>(std::lround(width * scale));
      const int scaled_height = static_cast<int>(std::lround(height * scale));
      const D2D_MATRIX_3X2_F matrix{static_cast<float>(scaled_width) / width, 0, 0, static_cast<float>(scaled_height) / height,
                                     static_cast<float>((width - scaled_width) / 2), static_cast<float>((height - scaled_height) / 2)};
      HRESULT hr = visual->SetTransform(matrix);
      if (SUCCEEDED(hr)) hr = visual->SetBitmapInterpolationMode(DCOMPOSITION_BITMAP_INTERPOLATION_MODE_LINEAR);
      if (FAILED(hr)) return hr;
      preview_scale = scale;
      ++statistics.transforms;
    }
    if (full) {
      const HRESULT hr = Upload(pixels, pixels.Bounds());
      if (FAILED(hr)) return hr;
    } else {
      if (scroll) {
        const auto& area = scroll->area;
        const RECT rect{area.left, area.top, area.right, area.bottom};
        const HRESULT hr = surface->Scroll(&rect, &rect, scroll->dx, scroll->dy);
        if (FAILED(hr)) return hr;
        ++statistics.scrolls;
      }
      for (const auto& area : damage) {
        const HRESULT hr = Upload(pixels, area);
        if (FAILED(hr)) return hr;
      }
    }
    // Publish all disjoint updates in one transaction, without waiting on
    // each display refresh or committing a separate frame for each rectangle.
    const HRESULT hr = composition->Commit();
    if (SUCCEEDED(hr)) ++statistics.commits;
    return hr;
  }
};

WindowPresenter::WindowPresenter() : impl_(std::make_unique<Impl>()) {}
WindowPresenter::~WindowPresenter() { Shutdown(); }

void WindowPresenter::Initialize(HWND window, bool force_gdi) {
  Shutdown();
  impl_->window = window;
  impl_->failure = S_OK;
  impl_->gdi_repaint = true;
  impl_->statistics = {};
  if (force_gdi) return;
  const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  impl_->owns_com = SUCCEEDED(apartment);
  if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) {
    impl_->UseGdi(apartment);
    return;
  }
  const HRESULT hr = impl_->CreateDevice();
  if (FAILED(hr)) impl_->UseGdi(hr);
}

bool WindowPresenter::Present(const Pixmap& pixels, std::span<const IntRect> damage, bool full, const Scroll* scroll, float preview_scale) {
  if (!impl_->composition || pixels.Width() <= 0 || pixels.Height() <= 0) return false;
  BOOL valid = FALSE;
  HRESULT hr = impl_->composition->CheckDeviceState(&valid);
  if (FAILED(hr) || !valid) {
    // A driver reset triggers WM_PAINT even when the document is unchanged.
    // Recover from the retained CPU image without rerasterizing the document.
    impl_->ReleaseDevice();
    hr = impl_->CreateDevice();
    full = true;
  }
  if (SUCCEEDED(hr)) hr = impl_->Submit(pixels, damage, full, scroll, preview_scale);
  if (FAILED(hr)) {
    impl_->UseGdi(hr);
    return false;
  }
  impl_->gdi_repaint = false;
  return true;
}

bool WindowPresenter::TakeGdiRepaint() { return std::exchange(impl_->gdi_repaint, false); }
bool WindowPresenter::IsComposed() const { return impl_->composition != nullptr; }
WindowPresenter::Statistics WindowPresenter::GetStatistics() const { return impl_->statistics; }

void WindowPresenter::ReportMemory() const {
  std::fprintf(stderr, "presenter backend=%s logical_surface=%.2f MiB fallback_hr=0x%08lx\n",
                  impl_->composition ? "directcomposition" : "gdi", static_cast<double>(impl_->width) * impl_->height * 4 / 1048576.0,
                  static_cast<unsigned long>(impl_->failure));
  const auto slots = std::count_if(impl_->uploads.begin(), impl_->uploads.end(), [](const auto& upload) { return upload != nullptr; });
  std::fprintf(stderr, "presenter upload_staging=%.2f MiB\n", slots * Impl::kUploadTile * Impl::kUploadTile * 4 / 1048576.0);
  if (!impl_->adapter) return;
  for (const auto segment : {DXGI_MEMORY_SEGMENT_GROUP_LOCAL, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL}) {
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    if (SUCCEEDED(impl_->adapter->QueryVideoMemoryInfo(0, segment, &info)))
      std::fprintf(stderr, "gpu_process %s_usage=%.2f MiB\n", segment == DXGI_MEMORY_SEGMENT_GROUP_LOCAL ? "local" : "nonlocal",
                      info.CurrentUsage / 1048576.0);
  }
}

void WindowPresenter::Shutdown() {
  impl_->ReleaseDevice();
  impl_->window = nullptr;
  if (std::exchange(impl_->owns_com, false)) CoUninitialize();
}

} // namespace rich_text
