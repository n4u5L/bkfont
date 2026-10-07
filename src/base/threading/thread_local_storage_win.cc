// Ported from: chromium/base/threading/thread_local_storage.cc
// Ported from: chromium/base/threading/thread_local_storage_win.cc

// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "thread_local_storage.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>

#include "base/immediate_crash.h"
#include "base/lean_windows.h"
#include "base/notreached.h"

namespace bkit::base {

namespace {

constexpr std::size_t kSlotCount = 256;
enum class VectorState : std::uintptr_t {
  kUninitialized = 0,
  kDestroying = 1,
  kDestroyed = 2,
  kInUse = 3,
};

struct Entry {
  void* data = nullptr;
  std::uint32_t version = 0;
};
using TlsVector = std::array<Entry, kSlotCount>;

struct Metadata {
  bool in_use = false;
  ThreadLocalStorage::Slot::Destructor destructor = nullptr;
  std::uint32_t version = 0;
  std::uint32_t sequence = 0;
};

std::atomic<DWORD> native_key{TLS_OUT_OF_INDEXES};
SRWLOCK metadata_lock = SRWLOCK_INIT;
std::array<Metadata, kSlotCount> metadata;
std::size_t last_assigned_slot = 0;
std::uint32_t sequence = 0;

class MetadataLock {
public:
  MetadataLock() {
    ::AcquireSRWLockExclusive(&metadata_lock);
  }
  ~MetadataLock() {
    ::ReleaseSRWLockExclusive(&metadata_lock);
  }
};

void SetVector(TlsVector* data, VectorState state) {
  const auto value = reinterpret_cast<std::uintptr_t>(data) | static_cast<std::uintptr_t>(state);
  if (!::TlsSetValue(native_key.load(std::memory_order_relaxed), reinterpret_cast<void*>(value))) ImmediateCrash();
}

TlsVector* GetVector() {
  const auto value = reinterpret_cast<std::uintptr_t>(::TlsGetValue(native_key.load(std::memory_order_relaxed)));
  assert((value & 3) != static_cast<std::uintptr_t>(VectorState::kDestroyed));
  return reinterpret_cast<TlsVector*>(value & ~std::uintptr_t{3});
}

void DestroyVector(TlsVector* heap_data) {
  TlsVector data = *heap_data;
  SetVector(&data, VectorState::kDestroying);
  delete heap_data;

  // No allocations after tearing down services. A destructor can populate
  // any slot, so rescan the whole table in reverse slot-creation order.
  // The limit counts the final empty scan, just as OnThreadExitInternal does.
  unsigned remaining_attempts = kSlotCount + 1;
  bool need_to_scan = true;
  while (need_to_scan) {
    need_to_scan = false;
    std::array<Metadata, kSlotCount> snapshot;
    {
      MetadataLock lock;
      snapshot = metadata;
    }
    std::array<std::size_t, kSlotCount> order;
    for (std::size_t i = 0; i < kSlotCount; ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&snapshot](std::size_t a, std::size_t b) {
      return snapshot[a].sequence > snapshot[b].sequence;
    });
    for (std::size_t i : order) {
      void* slot_value = data[i].data;
      const Metadata& slot = snapshot[i];
      if (!slot_value || !slot.in_use || data[i].version != slot.version || !slot.destructor) continue;
      data[i].data = nullptr;
      slot.destructor(slot_value);
      need_to_scan = true;
    }
    if (--remaining_attempts == 0) NOTREACHED();
  }
  SetVector(nullptr, VectorState::kDestroyed);
}

void NTAPI OnThreadExit(PVOID, DWORD reason, PVOID) {
  if (reason != DLL_THREAD_DETACH && reason != DLL_PROCESS_DETACH) return;
  if (native_key.load(std::memory_order_relaxed) == TLS_OUT_OF_INDEXES) return;
  TlsVector* data = GetVector();
  if (data) DestroyVector(data);
}

TlsVector* ConstructVector() {
  DWORD key = native_key.load(std::memory_order_relaxed);
  if (key == TLS_OUT_OF_INDEXES) {
    key = ::TlsAlloc();
    if (key == TLS_OUT_OF_INDEXES) ImmediateCrash();
    DWORD expected = TLS_OUT_OF_INDEXES;
    if (!native_key.compare_exchange_strong(expected, key, std::memory_order_relaxed)) {
      if (!::TlsFree(key)) ImmediateCrash();
    }
  }

  // Publish a stack vector before allocating; reentrant Set calls during
  // allocation must be copied into the permanent vector too.
  TlsVector temporary{};
  SetVector(&temporary, VectorState::kInUse);
  auto* data = new TlsVector;
  *data = temporary;
  SetVector(data, VectorState::kInUse);
  return data;
}

} // namespace

ThreadLocalStorage::Slot::Slot(Destructor destructor) {
  const DWORD key = native_key.load(std::memory_order_relaxed);
  if (key == TLS_OUT_OF_INDEXES || !::TlsGetValue(key)) ConstructVector();
  {
    MetadataLock lock;
    for (std::size_t i = 0; i < kSlotCount; ++i) {
      const std::size_t candidate = (last_assigned_slot + 1 + i) % kSlotCount;
      Metadata& slot = metadata[candidate];
      if (slot.in_use) continue;
      slot.in_use = true;
      slot.destructor = destructor;
      slot.sequence = ++sequence;
      last_assigned_slot = candidate;
      slot_ = candidate;
      version_ = slot.version;
      break;
    }
  }
  if (slot_ == kSlotCount) ImmediateCrash();
}

ThreadLocalStorage::Slot::~Slot() {
  MetadataLock lock;
  Metadata& slot = metadata[slot_];
  slot.in_use = false;
  slot.destructor = nullptr;
  ++slot.version;
}

void* ThreadLocalStorage::Slot::Get() const {
  TlsVector* data = GetVector();
  return data && (*data)[slot_].version == version_ ? (*data)[slot_].data : nullptr;
}

void ThreadLocalStorage::Slot::Set(void* value) {
  TlsVector* data = GetVector();
  if (!data) {
    if (!value) return;
    data = ConstructVector();
  }
  (*data)[slot_] = {value, version_};
}

// Match Chromium's native TLS exit hook. Force the callback into the final
// image even when this translation unit is linked from a static library.
#ifdef _WIN64
#pragma comment(linker, "/INCLUDE:_tls_used")
#pragma comment(linker, "/INCLUDE:bkit_thread_local_storage_callback")
#else
#pragma comment(linker, "/INCLUDE:__tls_used")
#pragma comment(linker, "/INCLUDE:_bkit_thread_local_storage_callback")
#endif

extern "C" {
#ifdef _WIN64
#pragma const_seg(".CRT$XLB")
extern const PIMAGE_TLS_CALLBACK bkit_thread_local_storage_callback;
const PIMAGE_TLS_CALLBACK bkit_thread_local_storage_callback = OnThreadExit;
#pragma const_seg()
#else
#pragma data_seg(".CRT$XLB")
PIMAGE_TLS_CALLBACK bkit_thread_local_storage_callback = OnThreadExit;
#pragma data_seg()
#endif
}

} // namespace bkit::base
