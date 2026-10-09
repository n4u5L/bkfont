#pragma once

// Windowed benchmarks report completed-frame FPS and memory, never an
// inverse CPU submission time. Each caller must wait for presentation before
// its frame callback returns. Windows memory includes process private bytes,
// working set, and DXGI process usage across adapters (node 0).
#ifdef _WIN32

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/lean_windows.h"
#include <dxgi1_4.h>
#include <psapi.h>
#include <wrl/client.h>

namespace bkit::example {

struct BenchmarkOptions {
  std::string scenario;
  int frames = 300;
  int warmup = 60;
  int repeats = 3;
  std::string workload = "sample";
  int items = 2048;
};

inline bool ParseBenchmarkCount(std::string_view text, int minimum, int maximum, int& value) {
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && value >= minimum && value <= maximum;
}

struct BenchmarkContext {
  const char* example;
  std::string_view scenario;
  const char* backend;
  int width, height;
  float scale;
  int render_divisor = 1;
  std::string_view workload = "sample";
  int items = 0;
};

struct BenchmarkMemory {
  double private_mib = 0, working_set_mib = 0;
  double gpu_local_mib = 0, gpu_nonlocal_mib = 0;
  double lifetime_peak_commit_mib = 0, lifetime_peak_working_set_mib = 0;
  bool gpu_available = false;
};

class BenchmarkMemorySampler {
public:
  BenchmarkMemorySampler() {
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())))) return;
    for (UINT i = 0;; ++i) {
      Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
      const HRESULT result = factory->EnumAdapters1(i, adapter.GetAddressOf());
      if (result == DXGI_ERROR_NOT_FOUND) break;
      if (FAILED(result)) {
        adapters_.clear();
        return;
      }
      DXGI_ADAPTER_DESC1 description{};
      if (FAILED(adapter->GetDesc1(&description))) {
        adapters_.clear();
        return;
      }
      if (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
      Microsoft::WRL::ComPtr<IDXGIAdapter3> memory_adapter;
      if (FAILED(adapter.As(&memory_adapter))) {
        adapters_.clear();
        return;
      }
      adapters_.push_back(std::move(memory_adapter));
    }
  }

  std::optional<BenchmarkMemory> Read() const {
    PROCESS_MEMORY_COUNTERS_EX process{};
    process.cb = sizeof process;
    if (!K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&process), sizeof process))
      return std::nullopt;
    constexpr double mib = 1048576.0;
    BenchmarkMemory memory;
    memory.private_mib = process.PrivateUsage / mib;
    memory.working_set_mib = process.WorkingSetSize / mib;
    memory.lifetime_peak_commit_mib = process.PeakPagefileUsage / mib;
    memory.lifetime_peak_working_set_mib = process.PeakWorkingSetSize / mib;
    memory.gpu_available = !adapters_.empty();
    for (const auto& adapter : adapters_) {
      DXGI_QUERY_VIDEO_MEMORY_INFO local{}, nonlocal{};
      if (FAILED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local)) ||
          FAILED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonlocal))) {
        memory.gpu_available = false;
        break;
      }
      memory.gpu_local_mib += local.CurrentUsage / mib;
      memory.gpu_nonlocal_mib += nonlocal.CurrentUsage / mib;
    }
    return memory;
  }

private:
  std::vector<Microsoft::WRL::ComPtr<IDXGIAdapter3>> adapters_;
};

// Peaks belong to this round, sampled every 30 frames and at its boundaries.
// OS lifetime peaks are labeled separately, since they also include startup
// and earlier scenarios. Baseline/end values expose retained cache growth.
class FrameBenchmark {
public:
  explicit FrameBenchmark(int frames) { frame_seconds_.reserve(frames); }

  bool SampleMemory(const BenchmarkMemorySampler& sampler) {
    const auto memory = sampler.Read();
    if (!memory) {
      std::fputs("benchmark: process memory query failed\n", stderr);
      return false;
    }
    if (!start_) start_ = memory;
    end_ = *memory;
    peak_.private_mib = std::max(peak_.private_mib, memory->private_mib);
    peak_.working_set_mib = std::max(peak_.working_set_mib, memory->working_set_mib);
    peak_.gpu_local_mib = std::max(peak_.gpu_local_mib, memory->gpu_local_mib);
    peak_.gpu_nonlocal_mib = std::max(peak_.gpu_nonlocal_mib, memory->gpu_nonlocal_mib);
    gpu_available_ = gpu_available_ && memory->gpu_available;
    return true;
  }

  void AddFrame(double seconds) {
    frame_seconds_.push_back(seconds);
    seconds_ += seconds;
  }

  void Report(const BenchmarkContext& context, const char* phase, int round) {
    std::sort(frame_seconds_.begin(), frame_seconds_.end(), std::greater<double>());
    const size_t low_count = std::max<size_t>(1, (frame_seconds_.size() + 99) / 100);
    double low_seconds = 0;
    for (size_t i = 0; i < low_count; ++i) low_seconds += frame_seconds_[i];
    // 1% low FPS is the reciprocal of the mean duration of the slowest 1%
    // of completed frames, not the mean of instantaneous FPS values.
    std::printf("{\"example\":\"%s\",\"scenario\":\"%.*s\",\"backend\":\"%s\",\"phase\":\"%s\",\"round\":%d,"
                "\"width\":%d,\"height\":%d,\"scale\":%.3f,\"render_divisor\":%d,\"frames\":%zu,"
                "\"workload\":\"%.*s\",\"items\":%d,\"pacing\":\"dwm_flush\","
                "\"fps\":%.3f,\"fps_1pct_low\":%.3f,",
                context.example, static_cast<int>(context.scenario.size()), context.scenario.data(), context.backend,
                phase, round, context.width, context.height, context.scale, context.render_divisor, frame_seconds_.size(),
                static_cast<int>(context.workload.size()), context.workload.data(), context.items,
                frame_seconds_.size() / seconds_, low_count / low_seconds);
    ReportMemory("private", start_->private_mib, end_.private_mib, peak_.private_mib, true);
    ReportMemory("working_set", start_->working_set_mib, end_.working_set_mib, peak_.working_set_mib, true);
    ReportMemory("gpu_local", start_->gpu_local_mib, end_.gpu_local_mib, peak_.gpu_local_mib, gpu_available_);
    ReportMemory("gpu_nonlocal", start_->gpu_nonlocal_mib, end_.gpu_nonlocal_mib, peak_.gpu_nonlocal_mib, gpu_available_);
    std::printf("\"lifetime_peak_commit_mib\":%.3f,\"lifetime_peak_working_set_mib\":%.3f,"
                "\"memory_sample_interval_frames\":30}\n", end_.lifetime_peak_commit_mib, end_.lifetime_peak_working_set_mib);
  }

private:
  static void ReportMemory(const char* name, double start, double end, double peak, bool available) {
    if (available)
      std::printf("\"%s_start_mib\":%.3f,\"%s_end_mib\":%.3f,\"%s_peak_sampled_mib\":%.3f,\"%s_delta_mib\":%.3f,",
                  name, start, name, end, name, peak, name, end - start);
    else
      std::printf("\"%s_start_mib\":null,\"%s_end_mib\":null,\"%s_peak_sampled_mib\":null,\"%s_delta_mib\":null,",
                  name, name, name, name);
  }

  std::vector<double> frame_seconds_;
  double seconds_ = 0;
  std::optional<BenchmarkMemory> start_;
  BenchmarkMemory end_, peak_;
  bool gpu_available_ = true;
};

template <class Frame>
bool RunFrameBenchmark(const BenchmarkOptions& options, const BenchmarkContext& context,
                       const BenchmarkMemorySampler& memory, const Frame& frame) {
  int step = 0;
  for (int round = 0; round <= options.repeats; ++round) {
    const int frames = round == 0 ? options.warmup : options.frames;
    if (!frames) continue;
    FrameBenchmark result(frames);
    if (!result.SampleMemory(memory)) return false;
    auto previous = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; ++i) {
      if (!frame(step++)) {
        std::fputs("benchmark: incomplete round; no result emitted\n", stderr);
        return false;
      }
      const auto now = std::chrono::steady_clock::now();
      result.AddFrame(std::chrono::duration<double>(now - previous).count());
      previous = now;
      // Sampling overhead stays in the next frame interval and therefore in
      // average FPS; reporting and sorting occur outside the measured round.
      if ((i + 1) % 30 == 0 && i + 1 < frames && !result.SampleMemory(memory)) return false;
    }
    if (!result.SampleMemory(memory)) return false;
    result.Report(context, round == 0 ? "warmup" : "measured", round);
  }
  return true;
}

} // namespace bkit::example

#endif // _WIN32
