// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license in LICENSE.
// Source: base/memory/ref_counted.h and base/atomic_ref_count.h.
// Sequence diagnostics removed; reference ownership and adoption are retained.
#pragma once
#include <atomic>
#include <cstdint>
#include <utility>
#include "base/compiler_specific.h"
#include "base/memory/scoped_refptr.h"
namespace bkfont::base {
namespace subtle {
class RefCountedBase {
public:
  bool HasOneRef() const {
    return ref_count_ == 1;
  }
  bool HasAtLeastOneRef() const {
    return ref_count_ >= 1;
  }
  void Adopted() const {
  }

protected:
  explicit RefCountedBase(StartRefCountFromZeroTag)
      : ref_count_(0) {
  }
  explicit RefCountedBase(StartRefCountFromOneTag)
      : ref_count_(1) {
  }
  ~RefCountedBase() = default;
  void AddRef() const {
    ++ref_count_;
  }
  bool Release() const {
    return --ref_count_ == 0;
  }

private:
  mutable std::uint32_t ref_count_;
};
class RefCountedThreadSafeBase {
public:
  bool HasOneRef() const {
    return ref_count_.load(std::memory_order_acquire) == 1;
  }
  bool HasAtLeastOneRef() const {
    return ref_count_.load(std::memory_order_acquire) >= 1;
  }
  void Adopted() const {
  }

protected:
  explicit RefCountedThreadSafeBase(StartRefCountFromZeroTag)
      : ref_count_(0) {
  }
  explicit RefCountedThreadSafeBase(StartRefCountFromOneTag)
      : ref_count_(1) {
  }
  ~RefCountedThreadSafeBase() = default;
  void AddRef() const {
    ref_count_.fetch_add(1, std::memory_order_relaxed);
  }
  bool Release() const {
    return ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1;
  }

private:
  mutable std::atomic<std::uint32_t> ref_count_;
};
} // namespace subtle
#define REQUIRE_ADOPTION_FOR_REFCOUNTED_TYPE() \
  using RefCountPreferenceTag = ::bkfont::base::subtle::StartRefCountFromOneTag

template <class T, typename Traits>
class RefCounted;

template <typename T>
struct DefaultRefCountedTraits {
  static void Destruct(const T* x) {
    RefCounted<T, DefaultRefCountedTraits>::DeleteInternal(x);
  }
};

template <class T, typename Traits = DefaultRefCountedTraits<T>>
class RefCounted : public subtle::RefCountedBase {
public:
  using RefCountPreferenceTag = subtle::StartRefCountFromZeroTag;

  RefCounted()
      : subtle::RefCountedBase(subtle::GetRefCountPreference<T>()) {
  }

  RefCounted(const RefCounted&) = delete;
  RefCounted& operator=(const RefCounted&) = delete;

  void AddRef() const {
    subtle::RefCountedBase::AddRef();
  }

  void Release() const {
    if (subtle::RefCountedBase::Release()) {
      // Prune the code paths which the static analyzer may take to simulate
      // object destruction. Use-after-free errors aren't possible given the
      // lifetime guarantees of the refcounting system.
      ANALYZER_SKIP_THIS_PATH();

      Traits::Destruct(static_cast<const T*>(this));
    }
  }

protected:
  ~RefCounted() = default;

private:
  friend struct DefaultRefCountedTraits<T>;
  template <typename U>
  static void DeleteInternal(const U* x) {
    delete x;
  }
};

// Forward declaration.
template <class T, typename Traits>
class RefCountedThreadSafe;

// Default traits for RefCountedThreadSafe<T>.  Deletes the object when its ref
// count reaches 0.  Overload to delete it on a different thread etc.
template <typename T>
struct DefaultRefCountedThreadSafeTraits {
  static void Destruct(const T* x) {
    // Delete through RefCountedThreadSafe to make child classes only need to be
    // friend with RefCountedThreadSafe instead of this struct, which is an
    // implementation detail.
    RefCountedThreadSafe<T, DefaultRefCountedThreadSafeTraits>::DeleteInternal(
        x);
  }
};

//
// A thread-safe variant of RefCounted<T>
//
//   class MyFoo : public base::RefCountedThreadSafe<MyFoo> {
//    ...
//   };
//
// If you're using the default trait, then you should add compile time
// asserts that no one else is deleting your object.  i.e.
//    private:
//     friend class base::RefCountedThreadSafe<MyFoo>;
//     ~MyFoo();
//
// We can use REQUIRE_ADOPTION_FOR_REFCOUNTED_TYPE() with RefCountedThreadSafe
// too. See the comment above the RefCounted definition for details.
template <class T, typename Traits = DefaultRefCountedThreadSafeTraits<T>>
class RefCountedThreadSafe : public subtle::RefCountedThreadSafeBase {
public:
  using RefCountPreferenceTag = subtle::StartRefCountFromZeroTag;

  RefCountedThreadSafe()
      : subtle::RefCountedThreadSafeBase(subtle::GetRefCountPreference<T>()) {
  }

  RefCountedThreadSafe(const RefCountedThreadSafe&) = delete;
  RefCountedThreadSafe& operator=(const RefCountedThreadSafe&) = delete;

  void AddRef() const {
    AddRefImpl(subtle::GetRefCountPreference<T>());
  }

  void Release() const {
    if (subtle::RefCountedThreadSafeBase::Release()) {
      ANALYZER_SKIP_THIS_PATH();
      Traits::Destruct(static_cast<const T*>(this));
    }
  }

protected:
  ~RefCountedThreadSafe() = default;

private:
  friend struct DefaultRefCountedThreadSafeTraits<T>;
  template <typename U>
  static void DeleteInternal(const U* x) {
    delete x;
  }

  void AddRefImpl(subtle::StartRefCountFromZeroTag) const {
    subtle::RefCountedThreadSafeBase::AddRef();
  }

  void AddRefImpl(subtle::StartRefCountFromOneTag) const {
    subtle::RefCountedThreadSafeBase::AddRef();
  }
};

//
// A thread-safe wrapper for some piece of data so we can place other
// things in scoped_refptrs<>.
//
template <typename T>
class RefCountedData
    : public base::RefCountedThreadSafe<base::RefCountedData<T>> {
public:
  RefCountedData()
      : data() {
  }
  RefCountedData(const T& in_value)
      : data(in_value) {
  }
  RefCountedData(T&& in_value)
      : data(std::move(in_value)) {
  }
  template <typename... Args>
  explicit RefCountedData(std::in_place_t, Args&&... args)
      : data(std::forward<Args>(args)...) {
  }

  T data;

private:
  friend class base::RefCountedThreadSafe<base::RefCountedData<T>>;
  ~RefCountedData() = default;
};

template <typename T>
bool operator==(const RefCountedData<T>& lhs, const RefCountedData<T>& rhs) {
  return lhs.data == rhs.data;
}

} // namespace bkfont::base
