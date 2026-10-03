// Ported from: blink/renderer/platform/wtf/vector.h
/*
 *  Copyright (C) 2005, 2006, 2007, 2008 Apple Inc. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this library; see the file COPYING.LIB.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 *
 */

#pragma once
#include <string.h>
#include <algorithm>
#include <compare>
#include <concepts>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <new>
#include <ostream>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include "base/containers/span.h"
#include "base/numerics/safe_conversions.h"
#include "allocator/partition_allocator.h"
#include "forward.h"
#include "hash_table_deleted_value_type.h"
#include "vector_traits.h"

namespace blink {
inline constexpr wtf_size_t kInitialVectorSize = 4;

// The ordinary placement construction branch of WTF ConstructTraits.
template <typename T, typename... Args>
T* VectorConstruct(void* location, Args&&... args) {
  return ::new (base::NotNullTag::kNotNull, location)
      T(std::forward<Args>(args)...);
}

template <typename T>
struct VectorElementComparer {
  template <typename U>
  static bool CompareElement(const T& left, const U& right) {
    return left == right;
  }
};

template <typename T>
struct VectorElementComparer<std::unique_ptr<T>> {
  template <typename U>
  static bool CompareElement(const std::unique_ptr<T>& left, const U& right) {
    return left.get() == right;
  }
};

template <typename T, typename Allocator>
struct VectorTypeOperations {
  static void Destruct(T* begin, T* end) {
    if constexpr (!VectorTraits<T>::kNeedsDestruction) {
      return;
    }
    for (T* cur = begin; cur != end; ++cur) {
      cur->~T();
    }
  }

  static void Initialize(T* begin, T* end) {
    if constexpr (VectorTraits<T>::kCanInitializeWithMemset) {
      const size_t bytes =
          reinterpret_cast<char*>(end) - reinterpret_cast<char*>(begin);
      if (bytes != 0) {
        memset(begin, 0, bytes);
      }
    } else {
      for (T* cur = begin; cur != end; ++cur) {
        VectorConstruct<T>(cur);
      }
    }
  }

  static void Move(T* const src, T* const src_end, T* const dst) {
    if (!src || !dst) [[unlikely]] {
      return;
    }
    if constexpr (VectorTraits<T>::kCanMoveWithMemcpy) {
      const size_t bytes = reinterpret_cast<const char*>(src_end) - reinterpret_cast<const char*>(src);
      memcpy(dst, src, bytes);
    } else {
      for (T *s = src, *d = dst; s != src_end; ++s, ++d) {
        VectorConstruct<T>(d, std::move(*s));
        s->~T();
      }
    }
  }

  static void MoveOverlapping(T* const src,
                              T* const src_end,
                              T* const dst) {
    if (!src || !dst || dst == src) [[unlikely]] {
      return;
    }
    if constexpr (VectorTraits<T>::kCanMoveWithMemcpy) {
      memmove(dst, src, reinterpret_cast<const char*>(src_end) - reinterpret_cast<const char*>(src));
    } else {
      if (dst < src) {
        // Moving towards the beginning cannot overwrite a later source slot.
        Move(src, src_end, dst);
        return;
      }
      T* s = src_end - 1;
      T* d = dst + (s - src);
      for (; s >= src; --s, --d) {
        VectorConstruct<T>(d, std::move(*s));
        s->~T();
      }
    }
  }

  static void Swap(T* const src, T* const src_end, T* const dst) {
    if constexpr (VectorTraits<T>::kCanMoveWithMemcpy) {
      std::swap_ranges(reinterpret_cast<char*>(src),
                       reinterpret_cast<char*>(src_end),
                       reinterpret_cast<char*>(dst));
    } else {
      std::swap_ranges(src, src_end, dst);
    }
  }

  static void Copy(const T* const src, const T* const src_end, T* dst) {
    if constexpr (VectorTraits<T>::kCanCopyWithMemcpy) {
      const size_t bytes = reinterpret_cast<const char*>(src_end) - reinterpret_cast<const char*>(src);
      if (src != src_end) {
        memcpy(dst, src, bytes);
      }
    } else {
      std::copy(src, src_end, dst);
    }
  }

  template <typename U>
  static void UninitializedCopy(const U* const src,
                                const U* const src_end,
                                T* dst) {
    if (!dst || !src) [[unlikely]] {
      return;
    }
    if constexpr (std::is_same_v<T, U> && VectorTraits<T>::kCanCopyWithMemcpy) {
      Copy(src, src_end, dst);
    } else {
      UninitializedTransform(src, src_end, dst, std::identity());
    }
  }

  template <typename InputIterator, typename Proj>
  static void UninitializedTransform(InputIterator src,
                                     InputIterator src_end,
                                     T* dst,
                                     Proj proj) {
    while (src != src_end) {
      VectorConstruct<T>(
          dst,
          std::invoke(proj, std::forward<decltype(*src)>(*src)));
      ++dst;
      ++src;
    }
  }

  static void UninitializedFill(T* const dst,
                                T* const dst_end,
                                const T& val) {
    if (!dst) [[unlikely]] {
      return;
    }
    if constexpr (VectorTraits<T>::kCanFillWithMemset) {
      memset(dst, static_cast<unsigned char>(val), dst_end - dst);
    } else {
      for (T* current = dst; current != dst_end; ++current) {
        VectorConstruct<T>(current, T(val));
      }
    }
  }

  static bool Compare(const T* a, const T* b, size_t size) {
    if constexpr (VectorTraits<T>::kCanCompareWithMemcmp)
      return memcmp(a, b, sizeof(T) * size) == 0;
    else
      return std::equal(a, a + size, b);
  }

  template <typename U>
  static bool CompareElement(const T& left, U&& right) {
    return VectorElementComparer<T>::CompareElement(left,
                                                    std::forward<U>(right));
  }
};

template <typename T, typename Allocator>
class VectorBufferBase {
public:
  VectorBufferBase(VectorBufferBase&&) = default;
  VectorBufferBase& operator=(VectorBufferBase&&) = default;

  void AllocateBuffer(wtf_size_t new_capacity) {
    size_t size_to_allocate = AllocationSize(new_capacity);
    buffer_ = Allocator::template AllocateVectorBacking<T>(size_to_allocate);
    capacity_ = static_cast<wtf_size_t>(size_to_allocate / sizeof(T));
  }

  size_t AllocationSize(size_t capacity) const {
    return Allocator::template QuantizedSize<T>(capacity);
  }

  T* Buffer() {
    return buffer_;
  }
  const T* Buffer() const {
    return buffer_;
  }
  wtf_size_t capacity() const {
    return capacity_;
  }

  void AcquireBuffer(VectorBufferBase&& other) {
    buffer_ = other.buffer_;
    capacity_ = other.capacity_;
  }

  struct OffsetRange final {
    OffsetRange()
        : begin(0),
          end(0) {
    }
    explicit OffsetRange(wtf_size_t begin, wtf_size_t end)
        : begin(begin),
          end(end) {
    }
    bool empty() const {
      return begin == end;
    }
    wtf_size_t begin;
    wtf_size_t end;
  };

protected:
  static VectorBufferBase AllocateTemporaryBuffer(wtf_size_t capacity) {
    VectorBufferBase buffer;
    buffer.AllocateBuffer(capacity);
    return buffer;
  }

  VectorBufferBase()
      : buffer_(nullptr),
        capacity_(0) {
  }
  VectorBufferBase(T* buffer, wtf_size_t capacity)
      : buffer_(buffer),
        capacity_(capacity) {
  }
  VectorBufferBase(HashTableDeletedValueType value)
      : buffer_(reinterpret_cast<T*>(-1)) {
  }

  bool IsHashTableDeletedValue() const {
    return buffer_ == reinterpret_cast<T*>(-1);
  }

  void SwapBuffers(VectorBufferBase& other) {
    std::swap(buffer_, other.buffer_);
    std::swap(capacity_, other.capacity_);
    std::swap(size_, other.size_);
  }

  T* buffer_;
  wtf_size_t capacity_;
  wtf_size_t size_;
};

template <typename T,
          wtf_size_t InlineCapacity,
          typename Allocator = PartitionAllocator>
class VectorBuffer;

template <typename T, typename Allocator>
class VectorBuffer<T, 0, Allocator> : protected VectorBufferBase<T, Allocator> {
private:
  using Base = VectorBufferBase<T, Allocator>;

public:
  using OffsetRange = typename Base::OffsetRange;

  VectorBuffer() = default;

  explicit VectorBuffer(wtf_size_t capacity) {
    // Calling malloc(0) might take a lock and may actually do an allocation
    // on some systems.
    if (capacity) {
      AllocateBuffer(capacity);
    }
  }

  explicit VectorBuffer(HashTableDeletedValueType value)
      : Base(value) {
  }

  void Destruct() {
    DeallocateBuffer(buffer_);
    buffer_ = nullptr;
  }

  void DeallocateBuffer(T* buffer_to_deallocate) {
    Allocator::FreeVectorBacking(buffer_to_deallocate);
  }

  bool ExpandBuffer(wtf_size_t new_capacity) {
    size_t size_to_allocate = AllocationSize(new_capacity);
    if (buffer_ && Allocator::ExpandVectorBacking(buffer_, size_to_allocate)) {
      capacity_ = static_cast<wtf_size_t>(size_to_allocate / sizeof(T));
      return true;
    }
    return false;
  }

  inline bool ShrinkBuffer(wtf_size_t new_capacity) {
    size_t size_to_allocate = AllocationSize(new_capacity);
    bool succeeded = false;
    if (Allocator::ShrinkVectorBacking(buffer_, AllocationSize(capacity()), size_to_allocate)) {
      capacity_ = static_cast<wtf_size_t>(size_to_allocate / sizeof(T));
      succeeded = true;
    }
    return succeeded;
  }

  void ResetBufferPointer() {
    buffer_ = nullptr;
    capacity_ = 0;
  }

  // See the other specialization for the meaning of |thisHole| and |otherHole|.
  // They are irrelevant in this case.
  void SwapVectorBuffer(VectorBuffer<T, 0, Allocator>& other,
                        OffsetRange this_hole,
                        OffsetRange other_hole) {
    Base::SwapBuffers(other);
  }

  using Base::AllocateBuffer;
  using Base::AllocationSize;

  using Base::Buffer;
  using Base::capacity;

  bool HasOutOfLineBuffer() const {
    // When InlineCapacity is 0 we have an out of line buffer if we have a
    // buffer.
    return IsOutOfLineBuffer(Buffer());
  }

protected:
  using Base::size_;

  bool IsOutOfLineBuffer(const T* buffer) const {
    return buffer;
  }

private:
  using Base::buffer_;
  using Base::capacity_;
};

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
class VectorBuffer : protected VectorBufferBase<T, Allocator> {
private:
  using Base = VectorBufferBase<T, Allocator>;

public:
  using OffsetRange = typename Base::OffsetRange;

  VectorBuffer()
      : Base(InlineBuffer(), InlineCapacity) {
  }

  explicit VectorBuffer(HashTableDeletedValueType value)
      : Base(value) {
  }
  bool IsHashTableDeletedValue() const {
    return Base::IsHashTableDeletedValue();
  }

  explicit VectorBuffer(wtf_size_t capacity)
      : Base(InlineBuffer(), InlineCapacity) {
    if (capacity > InlineCapacity) {
      Base::AllocateBuffer(capacity);
    }
  }

  VectorBuffer(const VectorBuffer&) = delete;
  VectorBuffer& operator=(const VectorBuffer&) = delete;

  void Destruct() {
    DeallocateBuffer(buffer_);
    buffer_ = nullptr;
  }

  void ReallyDeallocateBuffer(T* buffer_to_deallocate) {
    Allocator::FreeVectorBacking(buffer_to_deallocate);
  }

  void DeallocateBuffer(T* buffer_to_deallocate) {
    if (buffer_to_deallocate != InlineBuffer()) [[unlikely]] {
      ReallyDeallocateBuffer(buffer_to_deallocate);
    }
  }

  bool ExpandBuffer(wtf_size_t new_capacity) {
    if (buffer_ == InlineBuffer())
      return false;

    size_t size_to_allocate = AllocationSize(new_capacity);
    if (buffer_ && Allocator::ExpandVectorBacking(buffer_, size_to_allocate)) {
      capacity_ = static_cast<wtf_size_t>(size_to_allocate / sizeof(T));
      return true;
    }
    return false;
  }

  inline bool ShrinkBuffer(wtf_size_t new_capacity) {
    if (new_capacity <= InlineCapacity) {
      // We need to switch to inlineBuffer.  Vector::ShrinkCapacity will
      // handle it.
      return false;
    }
    size_t new_size = AllocationSize(new_capacity);
    bool succeeded = false;
    if (Allocator::ShrinkVectorBacking(buffer_, AllocationSize(capacity()), new_size)) {
      capacity_ = static_cast<wtf_size_t>(new_size / sizeof(T));
      succeeded = true;
    }
    return succeeded;
  }

  void ResetBufferPointer() {
    buffer_ = InlineBuffer();
    capacity_ = InlineCapacity;
  }

  void AllocateBuffer(wtf_size_t new_capacity) {
    if (new_capacity > InlineCapacity) {
      Base::AllocateBuffer(new_capacity);
    } else {
      ResetBufferPointer();
    }
  }

  size_t AllocationSize(size_t capacity) const {
    if (capacity <= InlineCapacity) {
      return kInlineBufferSize;
    }
    return Base::AllocationSize(capacity);
  }

  // Swap two vector buffers, both of which have the same non-zero inline
  // capacity.
  //
  // If the data is in an out-of-line buffer, we can just pass the pointers
  // across the two buffers.  If the data is in an inline buffer, we need to
  // either swap or move each element, depending on whether each slot is
  // occupied or not.
  //
  // Further complication comes from the fact that VectorBuffer is also used as
  // the backing store of a Deque.  Deque allocates the objects like a ring
  // buffer, so there may be a "hole" (unallocated region) in the middle of the
  // buffer. This function assumes elements in a range [buffer_, buffer_ +
  // size_) are all allocated except for elements within |thisHole|. The same
  // applies for |other.buffer_| and |otherHole|.
  void SwapVectorBuffer(VectorBuffer<T, InlineCapacity, Allocator>& other,
                        OffsetRange this_hole,
                        OffsetRange other_hole) {
    using TypeOperations = VectorTypeOperations<T, Allocator>;

    if (Buffer() != InlineBuffer() && other.Buffer() != other.InlineBuffer()) {
      Base::SwapBuffers(other);
      return;
    }

    // Otherwise, we at least need to move some elements from one inline buffer
    // to another.
    //
    // Terminology: "source" is a place from which elements are copied, and
    // "destination" is a place to which elements are copied. thisSource or
    // otherSource can be empty (represented by nullptr) when this range or
    // other range is in an out-of-line buffer.
    //
    // We first record which range needs to get moved and where elements in such
    // a range will go. Elements in an inline buffer will go to the other
    // buffer's inline buffer. Elements in an out-of-line buffer won't move,
    // because we can just swap pointers of out-of-line buffers.
    T* this_source_begin = nullptr;
    wtf_size_t this_source_size = 0;
    T* this_destination_begin = nullptr;
    if (Buffer() == InlineBuffer()) {
      this_source_begin = Buffer();
      this_source_size = size_;
      this_destination_begin = other.InlineBuffer();
    } else {
      // We don't need the hole information for an out-of-line buffer.
      this_hole.begin = this_hole.end = 0;
    }
    T* other_source_begin = nullptr;
    wtf_size_t other_source_size = 0;
    T* other_destination_begin = nullptr;
    if (other.Buffer() == other.InlineBuffer()) {
      other_source_begin = other.Buffer();
      other_source_size = other.size_;
      other_destination_begin = InlineBuffer();
    } else {
      other_hole.begin = other_hole.end = 0;
    }

    // Next, we mutate members and do other bookkeeping. We do pointer swapping
    // (for out-of-line buffers) here if we can. From now on, don't assume
    // buffer() or capacity() maintains their original values.
    std::swap(capacity_, other.capacity_);
    if (this_source_begin && !other_source_begin) { // Our buffer is inline, theirs is not.
      buffer_ = other.Buffer();
      other.buffer_ = other.InlineBuffer();
      std::swap(size_, other.size_);
    } else if (!this_source_begin && other_source_begin) { // Their buffer is inline, ours is not.
      other.buffer_ = Buffer();
      buffer_ = InlineBuffer();
      std::swap(size_, other.size_);
    } else { // Both buffers are inline.
      std::swap(size_, other.size_);
    }

    // We are ready to move elements. We determine an action for each "section",
    // which is a contiguous range such that all elements in the range are
    // treated similarly.
    wtf_size_t section_begin = 0;
    while (section_begin < InlineCapacity) {
      // To determine the end of this section, we list up all the boundaries
      // where the "occupiedness" may change.
      wtf_size_t section_end = InlineCapacity;
      if (this_source_begin && section_begin < this_source_size)
        section_end = std::min(section_end, this_source_size);
      if (!this_hole.empty() && section_begin < this_hole.begin)
        section_end = std::min(section_end, this_hole.begin);
      if (!this_hole.empty() && section_begin < this_hole.end)
        section_end = std::min(section_end, this_hole.end);
      if (other_source_begin && section_begin < other_source_size)
        section_end = std::min(section_end, other_source_size);
      if (!other_hole.empty() && section_begin < other_hole.begin)
        section_end = std::min(section_end, other_hole.begin);
      if (!other_hole.empty() && section_begin < other_hole.end)
        section_end = std::min(section_end, other_hole.end);

      // Is the |sectionBegin|-th element of |thisSource| occupied?
      bool this_occupied = false;
      if (this_source_begin && section_begin < this_source_size) {
        // Yes, it's occupied, unless the position is in a hole.
        if (this_hole.empty() || section_begin < this_hole.begin || section_begin >= this_hole.end)
          this_occupied = true;
      }
      bool other_occupied = false;
      if (other_source_begin && section_begin < other_source_size) {
        if (other_hole.empty() || section_begin < other_hole.begin || section_begin >= other_hole.end)
          other_occupied = true;
      }

      if (this_occupied && other_occupied) {
        // Both occupied; swap them. In this case, one's destination must be the
        // other's source (i.e. both ranges are in inline buffers).
        TypeOperations::Swap(this_source_begin + section_begin,
                             this_source_begin + section_end,
                             other_source_begin + section_begin);
      } else if (this_occupied) {
        // Move from ours to theirs.
        TypeOperations::Move(this_source_begin + section_begin,
                             this_source_begin + section_end,
                             this_destination_begin + section_begin);
      } else if (other_occupied) {
        // Move from theirs to ours.
        TypeOperations::Move(other_source_begin + section_begin,
                             other_source_begin + section_end,
                             other_destination_begin + section_begin);
      } else {
        // Both empty; nothing to do.
      }

      section_begin = section_end;
    }
  }

  using Base::Buffer;
  using Base::capacity;

  bool HasOutOfLineBuffer() const {
    return IsOutOfLineBuffer(Buffer());
  }

protected:
  using Base::size_;

  bool IsOutOfLineBuffer(const T* buffer) const {
    return buffer && buffer != InlineBuffer();
  }

private:
  using Base::buffer_;
  using Base::capacity_;

  static const wtf_size_t kInlineBufferSize = InlineCapacity * sizeof(T);
  T* InlineBuffer() {
    return reinterpret_cast<T*>(inline_buffer_);
  }
  const T* InlineBuffer() const {
    return reinterpret_cast<const T*>(inline_buffer_);
  }

  alignas(T) char inline_buffer_[kInlineBufferSize];
  template <typename U, wtf_size_t inlineBuffer, typename V>
  friend class Deque;
};

// UncheckedIterator<T> is just a wrapper of a T pointer with no bounds
// checking, and the default iterator implementation of blink::Vector.
template <typename T>
class UncheckedIterator {
public:
  using difference_type = std::ptrdiff_t;
  using value_type = std::remove_cv_t<T>;
  using pointer = T*;
  using reference = T&;
  using iterator_category = std::contiguous_iterator_tag;
  using iterator_concept = std::contiguous_iterator_tag;

  constexpr UncheckedIterator() = default;
  explicit UncheckedIterator(T* cur)
      : current_(cur) {
  }
  UncheckedIterator(const UncheckedIterator& other) = default;
  ~UncheckedIterator() = default;

  UncheckedIterator& operator=(const UncheckedIterator& other) = default;

  friend constexpr bool operator==(const UncheckedIterator& lhs,
                                   const UncheckedIterator& rhs) {
    return lhs.current_ == rhs.current_;
  }
  friend auto operator<=>(const UncheckedIterator& lhs,
                          const UncheckedIterator& rhs) {
    return lhs.current_ <=> rhs.current_;
  }

  UncheckedIterator& operator++() {
    ++current_;
    return *this;
  }
  UncheckedIterator operator++(int) {
    auto old = *this;
    ++current_;
    return old;
  }
  UncheckedIterator& operator--() {
    --current_;
    return *this;
  }
  UncheckedIterator operator--(int) {
    auto old = *this;
    --current_;
    return old;
  }
  UncheckedIterator& operator+=(difference_type rhs) {
    current_ += rhs;
    return *this;
  }
  UncheckedIterator operator+(difference_type rhs) const {
    auto it = *this;
    it += rhs;
    return it;
  }
  friend UncheckedIterator operator+(
      difference_type lhs,
      const UncheckedIterator& rhs) {
    return rhs + lhs;
  }
  UncheckedIterator& operator-=(difference_type rhs) {
    current_ -= rhs;
    return *this;
  }
  UncheckedIterator operator-(difference_type rhs) const {
    auto it = *this;
    it -= rhs;
    return it;
  }
  friend difference_type operator-(const UncheckedIterator& lhs,
                                   const UncheckedIterator& rhs) {
    return lhs.current_ - rhs.current_;
  }

  T& operator*() const {
    return *current_;
  }
  T* operator->() const {
    return current_;
  }
  T& operator[](difference_type rhs) const {
    return current_[rhs];
  }

  friend std::ostream& operator<<(std::ostream& out,
                                  const UncheckedIterator& rhs) {
    return out << "UncheckedIterator {current_:" << rhs.current_ << "}";
  }

private:
  // Allow current_ access from UncheckedIterator<U>.
  template <typename>
  friend class UncheckedIterator;

  T* current_ = nullptr;
};

template <typename T,
          wtf_size_t InlineCapacity,
          typename Allocator,
          typename Range,
          typename Proj>
concept VectorCanAssignFromRange =
    std::ranges::input_range<Range> && std::ranges::sized_range<Range> && std::indirectly_unary_invocable<Proj, std::ranges::iterator_t<Range>> &&
    // This prevents accidental fallback from the more efficient code paths.
    (!std::is_base_of_v<Vector<T, InlineCapacity, Allocator>,
                        std::decay_t<Range>>
     || !std::is_same_v<Proj, std::identity>);

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
class Vector : private VectorBuffer<T, InlineCapacity, Allocator> {
  using Base = VectorBuffer<T, InlineCapacity, Allocator>;
  using TypeOperations = VectorTypeOperations<T, Allocator>;
  using OffsetRange = typename Base::OffsetRange;

public:
  using ValueType = T;
  using value_type = T;
  using size_type = wtf_size_t;
  using reference = value_type&;
  using const_reference = const value_type&;
  using pointer = value_type*;
  using const_pointer = const value_type*;

  using iterator = UncheckedIterator<T>;
  using const_iterator = UncheckedIterator<const T>;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  static constexpr bool SupportsInlineCapacity() {
    return InlineCapacity > 0;
  }

  // Create an empty vector.
  inline Vector();
  // Create a vector containing the specified number of default-initialized
  // elements. Requires T to have a default constructor.
  inline explicit Vector(wtf_size_t);
  // Create a vector containing the specified number of elements, each of which
  // is copy initialized from the specified value.
  inline Vector(wtf_size_t, const T&);

  // HashTable support
  Vector(HashTableDeletedValueType value)
      : Base(value) {
  }
  bool IsHashTableDeletedValue() const {
    return Base::IsHashTableDeletedValue();
  }

  // Copying.
  Vector(const Vector&);
  template <wtf_size_t otherCapacity>
  explicit Vector(const Vector<T, otherCapacity, Allocator>&);

  Vector& operator=(const Vector&);
  template <wtf_size_t otherCapacity>
  Vector& operator=(const Vector<T, otherCapacity, Allocator>&);

  // Creates a vector with elements copied or moved from an input and sized
  // range, with optional projection. To move elements, use
  // base::RangeAsRvalues(std::move(range)) as the first parameter.
  template <typename Range, typename Proj = std::identity>
    requires VectorCanAssignFromRange<T, InlineCapacity, Allocator, Range, Proj>
  explicit Vector(Range&&, Proj = {});

  // Replaces the vector with elements copied or moved from an input and sized
  // range. To move elements, use base::RangeAsRvalues(std::move(range)) as the
  // first parameter.
  template <typename Range, typename Proj = std::identity>
    requires VectorCanAssignFromRange<T, InlineCapacity, Allocator, Range, Proj>
  void assign(Range&&, Proj = {});

  // Moving.
  Vector(Vector&&);
  Vector& operator=(Vector&&);

  // Construct with an initializer list. You can do e.g.
  //     Vector<int> v({1, 2, 3});
  // or
  //     v = {4, 5, 6};
  Vector(std::initializer_list<T> elements);
  Vector& operator=(std::initializer_list<T> elements);

  // Basic inquiry about the vector's state.
  //
  // capacity() is the maximum number of elements that the Vector can hold
  // without a reallocation. It can be zero.
  wtf_size_t size() const {
    return size_;
  }
  wtf_size_t capacity() const {
    return Base::capacity();
  }
  size_t CapacityInBytes() const {
    return Base::AllocationSize(capacity());
  }
  bool empty() const {
    return !size();
  }

  // at() and operator[]: Obtain the reference of the element that is located
  // at the given index. The reference may be invalidated on a reallocation.
  //
  // at() can be used in cases like:
  //     pointerToVector->at(1);
  // instead of:
  //     (*pointerToVector)[1];
  T& at(wtf_size_t i) {
    return Base::Buffer()[i];
  }
  const T& at(wtf_size_t i) const {
    return Base::Buffer()[i];
  }

  T& operator[](wtf_size_t i) {
    return at(i);
  }
  const T& operator[](wtf_size_t i) const {
    return at(i);
  }

  // Returns a base::span representing the whole data.
  // The base::span is valid until this Vector is modified.
  explicit operator base::span<T>() {
    return {data(), size()};
  }
  explicit operator base::span<const T>() {
    return {data(), size()};
  }
  explicit operator std::span<T>() {
    return {data(), size()};
  }
  explicit operator std::span<const T>() const {
    return {data(), size()};
  }

  // Return a pointer to the front of the backing buffer. Those pointers get
  // invalidated on a reallocation.
  T* data() {
    return Base::Buffer();
  }
  const T* data() const {
    return Base::Buffer();
  }

  // Iterators and reverse iterators. They are invalidated on a reallocation.
  //
  // When working with a subrange of a Vector, use base::span to represent
  // the range instead of a pair of iterators.
  //
  iterator begin() {
    return iterator(data());
  }
  iterator end() {
    return iterator(DataEnd());
  }
  const_iterator begin() const {
    return const_iterator(data());
  }
  const_iterator end() const {
    return const_iterator(DataEnd());
  }

  reverse_iterator rbegin() {
    return reverse_iterator(end());
  }
  reverse_iterator rend() {
    return reverse_iterator(begin());
  }
  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(end());
  }
  const_reverse_iterator rend() const {
    return const_reverse_iterator(begin());
  }

  // Quick access to the first and the last element. It is invalid to call
  // these functions when the vector is empty.
  T& front() {
    return at(0);
  }
  const T& front() const {
    return at(0);
  }
  T& back() {
    return at(size() - 1);
  }
  const T& back() const {
    return at(size() - 1);
  }

  // Searching.
  //
  // Comparisons are done in terms of compareElement(), which is usually
  // operator==(). find() and reverseFind() returns an index of the element
  // that is found first. If no match is found, kNotFound will be returned.
  template <typename U>
  bool Contains(const U&) const;
  template <typename U>
  wtf_size_t Find(const U&) const;
  template <typename U>
  wtf_size_t ReverseFind(const U&) const;

  // Resize the vector to the specified size.
  //
  // These three functions are essentially similar. They differ in that
  // Grow() and resize() require T to have a default constructor.
  //
  // When a vector shrinks, the extra elements in the back will be destructed.
  // All the iterators pointing to a to-be-destructed element will be
  // invalidated.
  //
  // When a vector grows, new elements will be added in the back, and they
  // will be default-initialized. A reallocation may happen in this case.
  void Shrink(wtf_size_t);
  void Grow(wtf_size_t);
  void resize(wtf_size_t);

  // Increase the capacity of the vector to at least |newCapacity|. The
  // elements in the vector are not affected. This function does not shrink
  // the size of the backing buffer, even if |newCapacity| is small. This
  // function may cause a reallocation.
  void reserve(wtf_size_t new_capacity);

  // This is similar to reserve() but must be called immediately after
  // the vector is default-constructed.
  void ReserveInitialCapacity(wtf_size_t initial_capacity);

  // Shrink the backing buffer to |new_capacity|. This function may cause a
  // reallocation.
  void ShrinkCapacity(wtf_size_t new_capacity);

  // Shrink the backing buffer so it can contain exactly |size()| elements.
  // This function may cause a reallocation.
  void shrink_to_fit() {
    ShrinkCapacity(size());
  }

  // Shrink the backing buffer if at least 50% of the vector's capacity is
  // unused. If it shrinks, the new buffer contains roughly 25% of unused
  // space. This function may cause a reallocation.
  void ShrinkToReasonableCapacity() {
    if (size() * 2 < capacity())
      ShrinkCapacity(size() + size() / 4 + 1);
  }

  // Remove all the elements. This function actually releases the backing
  // buffer, thus any iterators will get invalidated (including begin()).
  void clear() {
    ShrinkCapacity(0);
  }

  // Insertion to the back. All of these functions except uncheckedAppend() may
  // cause a reallocation.
  //
  // push_back(value)
  //     Insert a single element to the back.
  // emplace_back(args...)
  //     Insert a single element constructed as T(args...) to the back. The
  //     element is constructed directly on the backing buffer with placement
  //     new.
  // Append(buffer, size)
  // AppendVector(vector)
  // AppendRange(begin, end)
  // AppendSpan(span)
  //     Insert multiple elements represented by (1) |buffer| and |size|
  //     (for append), (2) |vector| (for AppendVector), (3) a pair of
  //     iterators (for AppendRange), or (4) |span| (for AppendSpan) to the
  //     back. Except for AppendRange, the elements will be copied. For
  //     AppendRange, the elements will be copied or moved depending on the
  //     iterators. For example, the elements will be moved if the iterators
  //     are from std::make_move_iterator().
  // UncheckedAppend(value)
  //     Insert a single element like push_back(), but this function assumes
  //     the vector has enough capacity such that it can store the new element
  //     without a reallocation. Using this function could improve the
  //     performance when you append many elements repeatedly.
  template <typename U>
  void push_back(U&&);
  template <typename... Args>
  T& emplace_back(Args&&...);
  T& emplace_back() {
    Grow(size_ + 1);
    return back();
  }
  template <typename U>
  void Append(const U*, wtf_size_t);
  template <typename U, wtf_size_t otherCapacity, typename V>
  void AppendVector(const Vector<U, otherCapacity, V>&);
  template <typename Iterator>
  void AppendRange(Iterator begin, Iterator end);
  template <typename U, size_t N, typename Ptr>
  void AppendSpan(base::span<U, N, Ptr>);
  template <typename U, size_t N>
  void AppendSpan(std::span<U, N> data) {
    Append(data.data(), base::checked_cast<wtf_size_t>(data.size()));
  }
  template <typename U>
  void UncheckedAppend(U&&);

  // Insertion to an arbitrary position. All of these functions will take
  // O(size())-time. All of the elements after |position| will be moved to
  // the new locations. |position| must be no more than size(). All of these
  // functions may cause a reallocation. In any case, all the iterators
  // pointing to an element after |position| will be invalidated.
  //
  // insert(position, value)
  //     Insert a single element at |position|, where |position| is an index.
  // insert(position, buffer, size)
  // InsertVector(position, vector)
  //     Insert multiple elements represented by either |buffer| and |size|
  //     or |vector| at |position|. The elements will be copied.
  // InsertAt(position, value)
  //     Insert a single element at |position|, where |position| is an iterator.
  // InsertAt(position, buffer, size)
  //     Insert multiple elements represented by either |buffer| and |size|
  //     or |vector| at |position|. The elements will be copied.
  template <typename U>
  void insert(wtf_size_t position, U&&);
  template <typename U>
  void insert(wtf_size_t position, const U*, wtf_size_t);
  template <typename U>
  void InsertAt(iterator position, U&&);
  template <typename U>
  void InsertAt(iterator position, const U*, wtf_size_t);
  template <typename U, wtf_size_t otherCapacity, typename OtherAllocator>
  void InsertVector(wtf_size_t position,
                    const Vector<U, otherCapacity, OtherAllocator>&);

  // Insertion to the front. All of these functions will take O(size())-time.
  // All of the elements in the vector will be moved to the new locations.
  // All of these functions may cause a reallocation. In any case, all the
  // iterators pointing to any element in the vector will be invalidated.
  //
  // push_front(value)
  //     Insert a single element to the front.
  // push_front(buffer, size)
  // PrependVector(vector)
  //     Insert multiple elements represented by either |buffer| and |size| or
  //     |vector| to the front. The elements will be copied.
  template <typename U>
  void push_front(U&&);
  template <typename U>
  void push_front(const U*, wtf_size_t);
  template <typename U, wtf_size_t otherCapacity, typename OtherAllocator>
  void PrependVector(const Vector<U, otherCapacity, OtherAllocator>&);

  // Remove an element or elements at the specified position. These functions
  // take O(size())-time. All of the elements after the removed ones will be
  // moved to the new locations. All the iterators pointing to any element
  // after |position| will be invalidated.
  void EraseAt(wtf_size_t position);
  void EraseAt(wtf_size_t position, wtf_size_t length);
  iterator erase(iterator position);
  iterator erase(iterator first, iterator last);
  // This is to prevent compilation of deprecated calls like 'vector.erase(0)'.
  void erase(std::nullptr_t) = delete;

  // Remove the last element. Unlike remove(), (1) this function is fast, and
  // (2) only iterators pointing to the last element will be invalidated. Other
  // references will remain valid.
  void pop_back() {
    Shrink(size() - 1);
  }

  // Filling the vector with the same value. If the vector has shrinked or
  // growed as a result of this call, those events may invalidate some
  // iterators. See comments for shrink() and grow().
  //
  // Fill(value, size) will resize the Vector to |size|, and then copy-assign
  // or copy-initialize all the elements.
  //
  // Fill(value) is a synonym for Fill(value, size()).
  //
  void Fill(const T&, wtf_size_t);
  void Fill(const T& val) {
    Fill(val, size());
  }

  // Swap two vectors quickly.
  void swap(Vector& other) {
    Base::SwapVectorBuffer(other, OffsetRange(), OffsetRange());
  }

  // Reverse the contents.
  void Reverse();

  // Maximum element count supported; allocating a vector
  // buffer with a larger count will fail.
  static size_t MaxCapacity() {
    return Allocator::template MaxElementCountInBackingStore<T>();
  }

  ~Vector() {
    if (!InlineCapacity) {
      if (!Base::Buffer()) [[likely]] {
        return;
      }
    }
    if (size_) [[likely]] {
      TypeOperations::Destruct(data(), DataEnd());
      size_ = 0;
    }
    Base::Destruct();
  }

private:
  template <typename, wtf_size_t, typename>
  friend class Vector;
  // Point the next of the last item. We must not dereference the return value.
  T* DataEnd() {
    return data() + size();
  }
  const T* DataEnd() const {
    return data() + size();
  }

  void ExpandCapacity(wtf_size_t new_min_capacity);
  T* ExpandCapacity(wtf_size_t new_min_capacity, T*);
  T* ExpandCapacity(wtf_size_t new_min_capacity, const T* data) {
    return ExpandCapacity(new_min_capacity, const_cast<T*>(data));
  }

  template <typename U>
  U* ExpandCapacity(wtf_size_t new_min_capacity, U*);
  template <typename U>
  void AppendSlowCase(U&&);

  bool HasInlineBuffer() const {
    return InlineCapacity && !this->HasOutOfLineBuffer();
  }

  void ReallocateBuffer(wtf_size_t);

  void SwapForMove(Vector&& other) {
    Base::SwapVectorBuffer(other, OffsetRange(), OffsetRange());
  }

  using Base::AllocateBuffer;
  using Base::AllocationSize;
  using Base::Buffer;
  using Base::size_;
  using Base::SwapVectorBuffer;
};

//
// Vector out-of-line implementation
//

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline Vector<T, InlineCapacity, Allocator>::Vector() {
  size_ = 0;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline Vector<T, InlineCapacity, Allocator>::Vector(wtf_size_t size)
    : Base(size) {
  size_ = size;
  TypeOperations::Initialize(data(), DataEnd());
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline Vector<T, InlineCapacity, Allocator>::Vector(wtf_size_t size,
                                                    const T& val)
    : Base(size) {
  size_ = size;
  TypeOperations::UninitializedFill(data(), DataEnd(), val);
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
Vector<T, InlineCapacity, Allocator>::Vector(const Vector& other)
    : Base(other.capacity()) {
  size_ = other.size();
  TypeOperations::UninitializedCopy(other.data(), other.DataEnd(), data());
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <wtf_size_t otherCapacity>
Vector<T, InlineCapacity, Allocator>::Vector(
    const Vector<T, otherCapacity, Allocator>& other)
    : Base(other.capacity()) {
  size_ = other.size();
  TypeOperations::UninitializedCopy(other.data(), other.DataEnd(), data());
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <typename Range, typename Proj>
  requires VectorCanAssignFromRange<T, InlineCapacity, Allocator, Range, Proj>
Vector<T, InlineCapacity, Allocator>::Vector(Range&& other, Proj proj)
    : Base(std::ranges::size(other)) {
  TypeOperations::UninitializedTransform(
      std::ranges::begin(other),
      std::ranges::end(other),
      data(),
      std::move(proj));
  size_ = std::ranges::size(other);
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
Vector<T, InlineCapacity, Allocator>&
Vector<T, InlineCapacity, Allocator>::operator=(
    const Vector<T, InlineCapacity, Allocator>& other) {
  if (&other == this) [[unlikely]] {
    return *this;
  }

  if (size() > other.size()) {
    Shrink(other.size());
  } else if (other.size() > capacity()) {
    clear();
    reserve(other.size());
  }

  TypeOperations::Copy(other.data(), other.data() + size(), data());
  TypeOperations::UninitializedCopy(
      other.data() + size(),
      other.DataEnd(),
      DataEnd());
  size_ = other.size();

  return *this;
}

inline bool TypelessPointersAreEqual(const void* a, const void* b) {
  return a == b;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <wtf_size_t otherCapacity>
Vector<T, InlineCapacity, Allocator>&
Vector<T, InlineCapacity, Allocator>::operator=(
    const Vector<T, otherCapacity, Allocator>& other) {
  // If the inline capacities match, we should call the more specific
  // template.  If the inline capacities don't match, the two objects
  // shouldn't be allocated the same address.

  if (size() > other.size()) {
    Shrink(other.size());
  } else if (other.size() > capacity()) {
    clear();
    reserve(other.size());
  }

  TypeOperations::Copy(other.data(), other.data() + size(), data());
  TypeOperations::UninitializedCopy(
      other.data() + size(),
      other.DataEnd(),
      DataEnd());
  size_ = other.size();

  return *this;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <typename Range, typename Proj>
  requires VectorCanAssignFromRange<T, InlineCapacity, Allocator, Range, Proj>
void Vector<T, InlineCapacity, Allocator>::assign(Range&& other, Proj proj) {
  if (std::ranges::size(other) > capacity()) {
    clear();
    reserve(std::ranges::size(other));
  } else {
    if (std::ranges::size(other) < size()) {
      Shrink(std::ranges::size(other));
    }
    TypeOperations::Destruct(data(), DataEnd());
  }

  TypeOperations::UninitializedTransform(
      std::ranges::begin(other),
      std::ranges::end(other),
      data(),
      std::move(proj));
  size_ = std::ranges::size(other);
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
Vector<T, InlineCapacity, Allocator>::Vector(
    Vector<T, InlineCapacity, Allocator>&& other) {
  size_ = 0;
  // It's a little weird to implement a move constructor using swap but this
  // way we don't have to add a move constructor to VectorBuffer.
  SwapForMove(std::move(other));
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
Vector<T, InlineCapacity, Allocator>&
Vector<T, InlineCapacity, Allocator>::operator=(
    Vector<T, InlineCapacity, Allocator>&& other) {
  // Free the old backing before taking the new buffer.
  clear();
  SwapForMove(std::move(other));
  return *this;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
Vector<T, InlineCapacity, Allocator>::Vector(std::initializer_list<T> elements)
    : Base(base::checked_cast<wtf_size_t>(elements.size())) {
  size_ = static_cast<wtf_size_t>(elements.size());
  TypeOperations::UninitializedCopy(elements.begin(), elements.end(), data());
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
Vector<T, InlineCapacity, Allocator>&
Vector<T, InlineCapacity, Allocator>::operator=(
    std::initializer_list<T> elements) {
  wtf_size_t input_size = base::checked_cast<wtf_size_t>(elements.size());
  if (size() > input_size) {
    Shrink(input_size);
  } else if (input_size > capacity()) {
    clear();
    reserve(input_size);
  }

  TypeOperations::Copy(elements.begin(), elements.begin() + size_, data());
  TypeOperations::UninitializedCopy(
      elements.begin() + size_,
      elements.end(),
      DataEnd());
  size_ = input_size;

  return *this;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <typename U>
bool Vector<T, InlineCapacity, Allocator>::Contains(const U& value) const {
  // Do not reuse Find because the compiler will generate extra code to
  // handle finding the kNotFound-th element in the array.  kNotFound is part
  // of wtf_size_t, but not used as an index due to runtime restrictions.  See
  // kNotFound.
  const T* b = data();
  const T* e = DataEnd();
  for (const T* iter = b; iter < e; ++iter) {
    if (TypeOperations::CompareElement(*iter, value)) {
      return true;
    }
  }
  return false;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <typename U>
wtf_size_t Vector<T, InlineCapacity, Allocator>::Find(const U& value) const {
  const T* b = data();
  const T* e = DataEnd();
  for (const T* iter = b; iter < e; ++iter) {
    if (TypeOperations::CompareElement(*iter, value))
      return static_cast<wtf_size_t>(iter - b);
  }
  return kNotFound;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <typename U>
wtf_size_t Vector<T, InlineCapacity, Allocator>::ReverseFind(
    const U& value) const {
  const T* b = data();
  const T* iter = DataEnd();
  while (iter > b) {
    --iter;
    if (TypeOperations::CompareElement(*iter, value))
      return static_cast<wtf_size_t>(iter - b);
  }
  return kNotFound;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
void Vector<T, InlineCapacity, Allocator>::Fill(const T& val,
                                                wtf_size_t new_size) {
  if (size() > new_size) {
    Shrink(new_size);
  } else if (new_size > capacity()) {
    clear();
    reserve(new_size);
  }

  std::fill(begin(), end(), val);
  TypeOperations::UninitializedFill(
      DataEnd(),
      data() + new_size,
      val);
  size_ = new_size;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
void Vector<T, InlineCapacity, Allocator>::ExpandCapacity(
    wtf_size_t new_min_capacity) {
  wtf_size_t old_capacity = capacity();
  wtf_size_t expanded_capacity = old_capacity;
  // We use a more aggressive expansion strategy for Vectors with inline
  // storage.  This is because they are more likely to be on the stack, so the
  // risk of heap bloat is minimized.  Furthermore, exceeding the inline
  // capacity limit is not supposed to happen in the common case and may
  // indicate a pathological condition or microbenchmark.
  if (InlineCapacity) {
    expanded_capacity *= 2;
  } else {
    // This cannot integer overflow.
    // On 64-bit, the "expanded" integer is 32-bit, and any encroachment
    // above 2^32 will fail allocation in allocateBuffer().  On 32-bit,
    // there's not enough address space to hold the old and new buffers.  In
    // addition, our underlying allocator is supposed to always fail on >
    // (2^31 - 1) allocations.
    expanded_capacity += (expanded_capacity / 4) + 1;
  }
  reserve(std::max(new_min_capacity,
                   std::max(kInitialVectorSize, expanded_capacity)));
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
T* Vector<T, InlineCapacity, Allocator>::ExpandCapacity(
    wtf_size_t new_min_capacity,
    T* ptr) {
  if (ptr < data() || ptr >= DataEnd()) {
    ExpandCapacity(new_min_capacity);
    return ptr;
  }
  size_t index = ptr - data();
  ExpandCapacity(new_min_capacity);
  return data() + index;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <typename U>
inline U* Vector<T, InlineCapacity, Allocator>::ExpandCapacity(
    wtf_size_t new_min_capacity,
    U* ptr) {
  ExpandCapacity(new_min_capacity);
  return ptr;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline void Vector<T, InlineCapacity, Allocator>::resize(wtf_size_t size) {
  if (size <= size_) {
    TypeOperations::Destruct(data() + size, DataEnd());
  } else {
    if (size > capacity())
      ExpandCapacity(size);
    TypeOperations::Initialize(DataEnd(), data() + size);
  }

  size_ = size;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
void Vector<T, InlineCapacity, Allocator>::Shrink(wtf_size_t size) {
  TypeOperations::Destruct(data() + size, DataEnd());
  size_ = size;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
void Vector<T, InlineCapacity, Allocator>::Grow(wtf_size_t size) {
  if (size > capacity())
    ExpandCapacity(size);
  TypeOperations::Initialize(DataEnd(), data() + size);
  size_ = size;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
void Vector<T, InlineCapacity, Allocator>::reserve(wtf_size_t new_capacity) {
  if (new_capacity <= capacity()) [[unlikely]] {
    return;
  }
  if (!data()) {
    Base::AllocateBuffer(new_capacity);
    return;
  }

  ReallocateBuffer(new_capacity);
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline void Vector<T, InlineCapacity, Allocator>::ReserveInitialCapacity(
    wtf_size_t initial_capacity) {
  if (initial_capacity > InlineCapacity) {
    Base::AllocateBuffer(initial_capacity);
  }
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
void Vector<T, InlineCapacity, Allocator>::ShrinkCapacity(
    wtf_size_t new_capacity) {
  if (new_capacity >= capacity())
    return;

  if (new_capacity < size())
    Shrink(new_capacity);

  T* old_buffer = data();
  if (new_capacity > 0) {
    if (Base::ShrinkBuffer(new_capacity)) {
      return;
    }

    ReallocateBuffer(new_capacity);
    return;
  }
  Base::ResetBufferPointer();
  Base::DeallocateBuffer(old_buffer);
}

// Templatizing these is better than just letting the conversion happen
// implicitly.
template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <typename U>
void Vector<T, InlineCapacity, Allocator>::push_back(U&& val) {
  if (size() != capacity()) [[likely]] {
    VectorConstruct<T>(
        DataEnd(),
        std::forward<U>(val));
    ++size_;
    return;
  }

  AppendSlowCase(std::forward<U>(val));
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <typename... Args>
T& Vector<T, InlineCapacity, Allocator>::emplace_back(
    Args&&... args) {
  if (size() == capacity()) [[unlikely]] {
    ExpandCapacity(size() + 1);
  }

  T* t =
      VectorConstruct<T>(
          DataEnd(),
          std::forward<Args>(args)...);
  ++size_;
  return *t;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <typename U>
void Vector<T, InlineCapacity, Allocator>::Append(const U* data,
                                                  wtf_size_t data_size) {
  wtf_size_t new_size = size_ + data_size;
  if (new_size > capacity()) {
    data = ExpandCapacity(new_size, data);
  }
  T* dest = DataEnd();
  TypeOperations::UninitializedCopy(
      data,
      &data[data_size],
      dest);
  size_ = new_size;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <typename U>
void Vector<T, InlineCapacity, Allocator>::AppendSlowCase(U&& val) {
  typename std::remove_reference<U>::type* ptr = &val;
  ptr = ExpandCapacity(size() + 1, ptr);

  VectorConstruct<T>(
      DataEnd(),
      std::forward<U>(*ptr));
  ++size_;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <typename U, wtf_size_t otherCapacity, typename OtherAllocator>
inline void Vector<T, InlineCapacity, Allocator>::AppendVector(
    const Vector<U, otherCapacity, OtherAllocator>& val) {
  Append(val.data(), val.size());
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <typename Iterator>
void Vector<T, InlineCapacity, Allocator>::AppendRange(Iterator begin,
                                                       Iterator end) {
  for (Iterator it = begin; it != end; ++it)
    push_back(*it);
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <typename U, size_t N, typename Ptr>
void Vector<T, InlineCapacity, Allocator>::AppendSpan(
    base::span<U, N, Ptr> data) {
  Append(data.data(), base::checked_cast<wtf_size_t>(data.size()));
}

// This version of append saves a branch in the case where you know that the
// vector's capacity is large enough for the append to succeed.
template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <typename U>
void Vector<T, InlineCapacity, Allocator>::UncheckedAppend(
    U&& val) {
  VectorConstruct<T>(
      DataEnd(),
      std::forward<U>(val));
  ++size_;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <typename U>
inline void Vector<T, InlineCapacity, Allocator>::insert(wtf_size_t position,
                                                         U&& val) {
  typename std::remove_reference<U>::type* data = &val;
  if (size() == capacity()) {
    data = ExpandCapacity(size() + 1, data);
  }
  T* spot = this->data() + position;
  TypeOperations::MoveOverlapping(spot, DataEnd(), spot + 1);
  VectorConstruct<T>(
      spot,
      std::forward<U>(*data));
  ++size_;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <typename U>
void Vector<T, InlineCapacity, Allocator>::insert(wtf_size_t position,
                                                  const U* data,
                                                  wtf_size_t data_size) {
  wtf_size_t new_size = size_ + data_size;
  if (new_size > capacity()) {
    data = ExpandCapacity(new_size, data);
  }
  T* spot = this->data() + position;
  TypeOperations::MoveOverlapping(spot, DataEnd(), spot + data_size);
  TypeOperations::UninitializedCopy(
      data,
      &data[data_size],
      spot);
  size_ = new_size;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <typename U>
void Vector<T, InlineCapacity, Allocator>::InsertAt(Vector::iterator position,
                                                    U&& val) {
  insert(base::checked_cast<wtf_size_t>(position - begin()), val);
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <typename U>
void Vector<T, InlineCapacity, Allocator>::InsertAt(Vector::iterator position,
                                                    const U* data,
                                                    wtf_size_t data_size) {
  insert(base::checked_cast<wtf_size_t>(position - begin()), data, data_size);
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <typename U, wtf_size_t otherCapacity, typename OtherAllocator>
inline void Vector<T, InlineCapacity, Allocator>::InsertVector(
    wtf_size_t position,
    const Vector<U, otherCapacity, OtherAllocator>& val) {
  insert(position, val.data(), val.size());
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <typename U>
inline void Vector<T, InlineCapacity, Allocator>::push_front(U&& val) {
  insert(0, std::forward<U>(val));
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <typename U>
void Vector<T, InlineCapacity, Allocator>::push_front(const U* data,
                                                      wtf_size_t data_size) {
  insert(0, data, data_size);
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <typename U, wtf_size_t otherCapacity, typename OtherAllocator>
inline void Vector<T, InlineCapacity, Allocator>::PrependVector(
    const Vector<U, otherCapacity, OtherAllocator>& val) {
  insert(0, val.data(), val.size());
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline void Vector<T, InlineCapacity, Allocator>::EraseAt(wtf_size_t position) {
  T* spot = data() + position;
  spot->~T();
  TypeOperations::MoveOverlapping(spot + 1, DataEnd(), spot);
  --size_;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline auto Vector<T, InlineCapacity, Allocator>::erase(iterator position)
    -> iterator {
  wtf_size_t index = static_cast<wtf_size_t>(position - begin());
  EraseAt(index);
  return begin() + index;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline auto Vector<T, InlineCapacity, Allocator>::erase(iterator first,
                                                        iterator last)
    -> iterator {
  const wtf_size_t index = static_cast<wtf_size_t>(first - begin());
  const wtf_size_t diff = static_cast<wtf_size_t>(std::distance(first, last));
  EraseAt(index, diff);
  return begin() + index;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline void Vector<T, InlineCapacity, Allocator>::EraseAt(wtf_size_t position,
                                                          wtf_size_t length) {
  if (!length)
    return;
  T* begin_spot = data() + position;
  T* end_spot = begin_spot + length;
  TypeOperations::Destruct(begin_spot, end_spot);
  TypeOperations::MoveOverlapping(end_spot, DataEnd(), begin_spot);
  size_ -= length;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline void Vector<T, InlineCapacity, Allocator>::Reverse() {
  for (wtf_size_t i = 0; i < size_ / 2; ++i)
    std::swap(at(i), at(size_ - 1 - i));
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline void swap(Vector<T, InlineCapacity, Allocator>& a,
                 Vector<T, InlineCapacity, Allocator>& b) {
  a.swap(b);
}

template <typename T,
          wtf_size_t InlineCapacityA,
          wtf_size_t InlineCapacityB,
          typename Allocator>
bool operator==(const Vector<T, InlineCapacityA, Allocator>& a,
                const Vector<T, InlineCapacityB, Allocator>& b) {
  if (a.size() != b.size())
    return false;
  if (a.empty())
    return true;
  return VectorTypeOperations<T, Allocator>::Compare(a.data(), b.data(), a.size());
}

template <typename T,
          wtf_size_t InlineCapacityA,
          wtf_size_t InlineCapacityB,
          typename Allocator>
inline bool operator!=(const Vector<T, InlineCapacityA, Allocator>& a,
                       const Vector<T, InlineCapacityB, Allocator>& b) {
  return !(a == b);
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
void Vector<T, InlineCapacity, Allocator>::ReallocateBuffer(
    wtf_size_t new_capacity) {
  if (new_capacity <= InlineCapacity) {
    if (HasInlineBuffer()) {
      Base::ResetBufferPointer();
      return;
    }
    // Shrinking to inline buffer from out-of-line one.
    T *old_begin = data(), *old_end = DataEnd();
    Base::ResetBufferPointer();
    TypeOperations::Move(old_begin, old_end, data());
    Base::DeallocateBuffer(old_begin);
    return;
  }
  // Shrinking/resizing to out-of-line buffer.
  VectorBufferBase<T, Allocator> temp_buffer =
      Base::AllocateTemporaryBuffer(new_capacity);
  TypeOperations::Move(data(), DataEnd(), temp_buffer.Buffer());
  Base::DeallocateBuffer(data());
  Base::AcquireBuffer(std::move(temp_buffer));
}

// Erase/EraseIf are based on C++20's uniform container erasure API:
// - https://eel.is/c++draft/libraryindex#:erase
// - https://eel.is/c++draft/libraryindex#:erase_if
template <typename T,
          wtf_size_t inline_capacity,
          typename Allocator,
          typename U>
wtf_size_t Erase(Vector<T, inline_capacity, Allocator>& v, const U& value) {
  auto it = std::remove(v.begin(), v.end(), value);
  wtf_size_t removed = base::checked_cast<wtf_size_t>(v.end() - it);
  v.erase(it, v.end());
  return removed;
}
template <typename T,
          wtf_size_t inline_capacity,
          typename Allocator,
          typename Pred>
wtf_size_t EraseIf(Vector<T, inline_capacity, Allocator>& v, Pred pred) {
  auto it = std::remove_if(v.begin(), v.end(), pred);
  wtf_size_t removed = base::checked_cast<wtf_size_t>(v.end() - it);
  v.erase(it, v.end());
  return removed;
}

// The WTF version of base::ToVector. This is more convenient to use than
// Vector::Vector(range[, proj]) in some cases, e.g. when a temporary vector is
// needed and the desired result type is the same as the deducted return type.
// See Vector::Vector(range, proj) and Vector::assign() about copying vs moving.
template <typename Range, typename Proj = std::identity>
  requires std::ranges::sized_range<Range> && std::ranges::input_range<Range> && std::indirectly_unary_invocable<Proj, std::ranges::iterator_t<Range>>
auto ToVector(Range&& range, Proj proj = {}) {
  using ProjectedType =
      std::projected<std::ranges::iterator_t<Range>, Proj>::value_type;
  return Vector<ProjectedType>(std::forward<Range>(range), std::move(proj));
}

} // namespace blink
