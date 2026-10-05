// Ported from: chromium/base/threading/thread_local_storage.cc
// Ported from: chromium/base/threading/thread_local_storage_posix.cc

// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.

#include "thread_local_storage_posix.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <pthread.h>

#include "base/immediate_crash.h"
#include "base/notreached.h"

namespace bkfont::base {

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

pthread_key_t native_key;
pthread_once_t native_key_once = PTHREAD_ONCE_INIT;
pthread_mutex_t metadata_mutex = PTHREAD_MUTEX_INITIALIZER;
std::array<Metadata, kSlotCount> metadata;
std::size_t last_assigned_slot = 0;
std::uint32_t sequence = 0;

class MetadataLock {
public:
  MetadataLock() {
    if (pthread_mutex_lock(&metadata_mutex) != 0) ImmediateCrash();
  }
  ~MetadataLock() {
    if (pthread_mutex_unlock(&metadata_mutex) != 0) ImmediateCrash();
  }
};

void SetVector(TlsVector* data, VectorState state) {
  const auto value = reinterpret_cast<std::uintptr_t>(data) | static_cast<std::uintptr_t>(state);
  if (pthread_setspecific(native_key, reinterpret_cast<void*>(value)) != 0) ImmediateCrash();
}

TlsVector* GetVector() {
  const auto value = reinterpret_cast<std::uintptr_t>(pthread_getspecific(native_key));
  assert((value & 3) != static_cast<std::uintptr_t>(VectorState::kDestroyed));
  return reinterpret_cast<TlsVector*>(value & ~std::uintptr_t{3});
}

void OnThreadExit(void* value) {
  const auto raw_value = reinterpret_cast<std::uintptr_t>(value);
  if ((raw_value & 3) == static_cast<std::uintptr_t>(VectorState::kDestroyed)) {
    SetVector(nullptr, VectorState::kUninitialized);
    return;
  }
  auto* heap_data = reinterpret_cast<TlsVector*>(raw_value & ~std::uintptr_t{3});
  TlsVector data = *heap_data;
  SetVector(&data, VectorState::kDestroying);
  delete heap_data;

  // No allocations after tearing down services. A destructor can populate
  // any slot, so rescan the whole table in reverse slot-creation order.
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

void CreateNativeKey() {
  if (pthread_key_create(&native_key, OnThreadExit) != 0) ImmediateCrash();
}

TlsVector* ConstructVector() {
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
  if (pthread_once(&native_key_once, CreateNativeKey) != 0) ImmediateCrash();
  if (!pthread_getspecific(native_key)) ConstructVector();
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

} // namespace bkfont::base
