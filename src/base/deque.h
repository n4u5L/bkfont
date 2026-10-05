// Ported from: blink/renderer/platform/wtf/deque.h
/*
 * Copyright (C) 2007, 2008 Apple Inc. All rights reserved.
 * Copyright (C) 2009 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

// FIXME: Could move what Vector and Deque share into a separate file.
// Deque doesn't actually use Vector.

#include <iterator>

#include "allocator/allocator.h"
#include "forward.h"
#include "vector.h"

namespace bkfont {

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
class DequeIteratorBase;
template <typename T, wtf_size_t InlineCapacity, typename Allocator>
class DequeIterator;
template <typename T, wtf_size_t InlineCapacity, typename Allocator>
class DequeConstIterator;

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
class Deque {

public:
  using value_type = T;
  typedef DequeIterator<T, InlineCapacity, Allocator> iterator;
  typedef DequeConstIterator<T, InlineCapacity, Allocator> const_iterator;
  typedef std::reverse_iterator<iterator> reverse_iterator;
  typedef std::reverse_iterator<const_iterator> const_reverse_iterator;

  Deque();

  ~Deque();

  Deque(const Deque&);
  Deque& operator=(const Deque&);
  Deque(Deque&&);
  Deque& operator=(Deque&&);

  void Swap(Deque&);

  wtf_size_t size() const {
    return start_ <= end_ ? end_ - start_ : end_ + buffer_.capacity() - start_;
  }
  bool empty() const {
    return start_ == end_;
  }

  iterator begin() {
    return iterator(this, start_);
  }
  iterator end() {
    return iterator(this, end_);
  }
  const_iterator begin() const {
    return const_iterator(this, start_);
  }
  const_iterator end() const {
    return const_iterator(this, end_);
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

  T& front() {
    return buffer_.Buffer()[start_];
  }
  const T& front() const {
    return buffer_.Buffer()[start_];
  }
  T TakeFirst();

  T& back() {
    return *(--end());
  }
  const T& back() const {
    return *(--end());
  }
  T TakeLast();

  T& at(wtf_size_t i) {
    wtf_size_t right = buffer_.capacity() - start_;
    return i < right ? buffer_.Buffer()[start_ + i]
                     : buffer_.Buffer()[i - right];
  }
  const T& at(wtf_size_t i) const {
    wtf_size_t right = buffer_.capacity() - start_;
    return i < right ? buffer_.Buffer()[start_ + i]
                     : buffer_.Buffer()[i - right];
  }

  T& operator[](wtf_size_t i) {
    return at(i);
  }
  const T& operator[](wtf_size_t i) const {
    return at(i);
  }

  template <typename U>
  void push_front(U&&);
  void erase(iterator&);
  void erase(const_iterator&);

  // STL compatibility.
  template <typename U>
  void push_back(U&&);
  void pop_back();
  void pop_front();
  template <typename... Args>
  void emplace_back(Args&&...);
  template <typename... Args>
  void emplace_front(Args&&...);

  void clear();

private:
  friend class DequeIteratorBase<T, InlineCapacity, Allocator>;

  class BackingBuffer : public VectorBuffer<T, InlineCapacity, Allocator> {
  private:
    using Base = VectorBuffer<T, InlineCapacity, Allocator>;
    using Base::size_;

    friend class Deque;

  public:
    BackingBuffer()
        : Base() {
    }
    explicit BackingBuffer(wtf_size_t capacity)
        : Base(capacity) {
    }
    BackingBuffer(const BackingBuffer&) = delete;
    BackingBuffer& operator=(const BackingBuffer&) = delete;

    void SetSize(wtf_size_t size) {
      size_ = size;
    }
  };

  typedef VectorTypeOperations<T, Allocator> TypeOperations;
  typedef DequeIteratorBase<T, InlineCapacity, Allocator> IteratorBase;

  void erase(wtf_size_t position);
  void DestroyAll();
  void ExpandCapacityIfNeeded();
  void ExpandCapacity();

  void SwapForMove(Deque&&);
  void SwapImpl(Deque&);

  BackingBuffer buffer_;
  wtf_size_t start_;
  wtf_size_t end_;
};

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
class DequeIteratorBase {

protected:
  constexpr DequeIteratorBase() = default;
  DequeIteratorBase(const Deque<T, InlineCapacity, Allocator>*, wtf_size_t);
  DequeIteratorBase(const DequeIteratorBase&);
  DequeIteratorBase& operator=(const DequeIteratorBase<T, 0, Allocator>&);
  ~DequeIteratorBase();

  void Assign(const DequeIteratorBase& other) {
    *this = other;
  }

  void Increment();
  void Decrement();

  T* Before() const;
  T* After() const;

  bool IsEqual(const DequeIteratorBase&) const;

private:
  Deque<T, InlineCapacity, Allocator>* deque_ = nullptr;
  unsigned index_ = 0;

  friend class Deque<T, InlineCapacity, Allocator>;
};

template <typename T,
          wtf_size_t InlineCapacity = 0,
          typename Allocator = PartitionAllocator>
class DequeIterator : public DequeIteratorBase<T, InlineCapacity, Allocator> {
private:
  typedef DequeIteratorBase<T, InlineCapacity, Allocator> Base;
  typedef DequeIterator<T, InlineCapacity, Allocator> Iterator;

public:
  typedef ptrdiff_t difference_type;
  typedef T value_type;
  typedef T* pointer;
  typedef T& reference;
  typedef std::bidirectional_iterator_tag iterator_category;

  constexpr DequeIterator() = default;
  DequeIterator(Deque<T, InlineCapacity, Allocator>* deque, wtf_size_t index)
      : Base(deque, index) {
  }

  DequeIterator(const Iterator& other)
      : Base(other) {
  }
  DequeIterator& operator=(const Iterator& other) {
    Base::Assign(other);
    return *this;
  }

  T& operator*() const {
    return *Base::After();
  }
  T* operator->() const {
    return Base::After();
  }

  bool operator==(const Iterator& other) const {
    return Base::IsEqual(other);
  }
  bool operator!=(const Iterator& other) const {
    return !Base::IsEqual(other);
  }

  Iterator& operator++() {
    Base::Increment();
    return *this;
  }

  Iterator operator++(int) {
    Iterator tmp = *this;
    ++*this;
    return tmp;
  }

  Iterator& operator--() {
    Base::Decrement();
    return *this;
  }

  Iterator operator--(int) {
    Iterator tmp = *this;
    --*this;
    return tmp;
  }
};

template <typename T,
          wtf_size_t InlineCapacity = 0,
          typename Allocator = PartitionAllocator>
class DequeConstIterator
    : public DequeIteratorBase<T, InlineCapacity, Allocator> {
private:
  typedef DequeIteratorBase<T, InlineCapacity, Allocator> Base;
  typedef DequeConstIterator<T, InlineCapacity, Allocator> Iterator;
  typedef DequeIterator<T, InlineCapacity, Allocator> NonConstIterator;

public:
  typedef ptrdiff_t difference_type;
  typedef T value_type;
  typedef const T* pointer;
  typedef const T& reference;
  typedef std::bidirectional_iterator_tag iterator_category;

  constexpr DequeConstIterator() = default;
  DequeConstIterator(const Deque<T, InlineCapacity, Allocator>* deque,
                     wtf_size_t index)
      : Base(deque, index) {
  }

  DequeConstIterator(const Iterator& other)
      : Base(other) {
  }
  DequeConstIterator(const NonConstIterator& other)
      : Base(other) {
  }
  DequeConstIterator& operator=(const Iterator& other) {
    Base::Assign(other);
    return *this;
  }
  DequeConstIterator& operator=(const NonConstIterator& other) {
    Base::Assign(other);
    return *this;
  }

  const T& operator*() const {
    return *Base::After();
  }
  const T* operator->() const {
    return Base::After();
  }

  bool operator==(const Iterator& other) const {
    return Base::IsEqual(other);
  }
  bool operator!=(const Iterator& other) const {
    return !Base::IsEqual(other);
  }

  Iterator& operator++() {
    Base::Increment();
    return *this;
  }

  Iterator operator++(int) {
    Iterator tmp = *this;
    ++*this;
    return tmp;
  }

  Iterator& operator--() {
    Base::Decrement();
    return *this;
  }

  Iterator operator--(int) {
    Iterator tmp = *this;
    --*this;
    return tmp;
  }
};

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline Deque<T, InlineCapacity, Allocator>::Deque()
    : start_(0),
      end_(0) {
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline Deque<T, InlineCapacity, Allocator>::Deque(const Deque& other)
    : buffer_(other.buffer_.capacity()),
      start_(other.start_),
      end_(other.end_) {
  const T* other_buffer = other.buffer_.Buffer();
  if (start_ <= end_) {
    TypeOperations::UninitializedCopy(
        other_buffer + start_,
        other_buffer + end_,
        buffer_.Buffer() + start_);
  } else {
    TypeOperations::UninitializedCopy(other_buffer, other_buffer + end_, buffer_.Buffer());
    TypeOperations::UninitializedCopy(
        other_buffer + start_,
        other_buffer + buffer_.capacity(),
        buffer_.Buffer() + start_);
  }
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline Deque<T, InlineCapacity, Allocator>&
Deque<T, InlineCapacity, Allocator>::operator=(const Deque& other) {
  Deque copy(other);
  Swap(copy);
  return *this;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline Deque<T, InlineCapacity, Allocator>::Deque(Deque&& other)
    : start_(0),
      end_(0) {
  SwapForMove(std::move(other));
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline Deque<T, InlineCapacity, Allocator>&
Deque<T, InlineCapacity, Allocator>::operator=(Deque&& other) {
  SwapForMove(std::move(other));
  return *this;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline void Deque<T, InlineCapacity, Allocator>::DestroyAll() {
  if (start_ <= end_) {
    TypeOperations::Destruct(buffer_.Buffer() + start_,
                             buffer_.Buffer() + end_);
  } else {
    TypeOperations::Destruct(buffer_.Buffer(), buffer_.Buffer() + end_);
    TypeOperations::Destruct(buffer_.Buffer() + start_,
                             buffer_.Buffer() + buffer_.capacity());
  }
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline Deque<T, InlineCapacity, Allocator>::~Deque() {
  if ((!InlineCapacity && !buffer_.Buffer()))
    return;
  if (!empty())
    DestroyAll();

  buffer_.Destruct();
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline void Deque<T, InlineCapacity, Allocator>::Swap(Deque& other) {
  return SwapImpl(other);
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline void Deque<T, InlineCapacity, Allocator>::SwapForMove(
    Deque&& other) {
  return SwapImpl(other);
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline void Deque<T, InlineCapacity, Allocator>::SwapImpl(
    Deque& other) {
  typename BackingBuffer::OffsetRange this_hole;
  if (start_ <= end_) {
    buffer_.SetSize(end_);
    this_hole.begin = 0;
    this_hole.end = start_;
  } else {
    buffer_.SetSize(buffer_.capacity());
    this_hole.begin = end_;
    this_hole.end = start_;
  }
  typename BackingBuffer::OffsetRange other_hole;
  if (other.start_ <= other.end_) {
    other.buffer_.SetSize(other.end_);
    other_hole.begin = 0;
    other_hole.end = other.start_;
  } else {
    other.buffer_.SetSize(other.buffer_.capacity());
    other_hole.begin = other.end_;
    other_hole.end = other.start_;
  }

  buffer_.SwapVectorBuffer(other.buffer_, this_hole, other_hole);

  std::swap(start_, other.start_);
  std::swap(end_, other.end_);
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline void Deque<T, InlineCapacity, Allocator>::clear() {
  DestroyAll();
  start_ = 0;
  end_ = 0;
  buffer_.DeallocateBuffer(buffer_.Buffer());
  buffer_.ResetBufferPointer();
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline void Deque<T, InlineCapacity, Allocator>::ExpandCapacityIfNeeded() {
  if (start_) {
    if (end_ + 1 != start_)
      return;
  } else if (end_) {
    if (end_ != buffer_.capacity() - 1)
      return;
  } else if (buffer_.capacity()) {
    return;
  }

  ExpandCapacity();
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
void Deque<T, InlineCapacity, Allocator>::ExpandCapacity() {
  wtf_size_t old_capacity = buffer_.capacity();
  T* old_buffer = buffer_.Buffer();
  wtf_size_t new_capacity = std::max(16u, old_capacity + old_capacity / 4 + 1);
  if (buffer_.ExpandBuffer(new_capacity)) {
    if (start_ <= end_) {
      // No adjustments to be done.
    } else {
      wtf_size_t new_start = buffer_.capacity() - (old_capacity - start_);
      TypeOperations::MoveOverlapping(
          old_buffer + start_,
          old_buffer + old_capacity,
          buffer_.Buffer() + new_start);
      start_ = new_start;
    }
    return;
  }
  buffer_.AllocateBuffer(new_capacity);
  if (start_ <= end_) {
    TypeOperations::Move(old_buffer + start_, old_buffer + end_, buffer_.Buffer() + start_);
  } else {
    TypeOperations::Move(old_buffer, old_buffer + end_, buffer_.Buffer());
    wtf_size_t new_start = buffer_.capacity() - (old_capacity - start_);
    TypeOperations::Move(old_buffer + start_, old_buffer + old_capacity, buffer_.Buffer() + new_start);
    start_ = new_start;
  }
  buffer_.DeallocateBuffer(old_buffer);
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline T Deque<T, InlineCapacity, Allocator>::TakeFirst() {
  T old_first = std::move(front());
  pop_front();
  return old_first;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline T Deque<T, InlineCapacity, Allocator>::TakeLast() {
  T old_last = std::move(back());
  pop_back();
  return old_last;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <typename U>
inline void Deque<T, InlineCapacity, Allocator>::push_back(U&& value) {
  ExpandCapacityIfNeeded();
  T* new_element = &buffer_.Buffer()[end_];
  if (end_ == buffer_.capacity() - 1)
    end_ = 0;
  else
    ++end_;
  VectorConstruct<T>(
      new_element,
      std::forward<U>(value));
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <typename U>
inline void Deque<T, InlineCapacity, Allocator>::push_front(U&& value) {
  ExpandCapacityIfNeeded();
  if (!start_)
    start_ = buffer_.capacity() - 1;
  else
    --start_;
  VectorConstruct<T>(
      &buffer_.Buffer()[start_],
      std::forward<U>(value));
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <typename... Args>
inline void Deque<T, InlineCapacity, Allocator>::emplace_back(Args&&... args) {
  ExpandCapacityIfNeeded();
  T* new_element = &buffer_.Buffer()[end_];
  if (end_ == buffer_.capacity() - 1)
    end_ = 0;
  else
    ++end_;
  VectorConstruct<T>(
      new_element,
      std::forward<Args>(args)...);
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
template <typename... Args>
inline void Deque<T, InlineCapacity, Allocator>::emplace_front(Args&&... args) {
  ExpandCapacityIfNeeded();
  if (!start_)
    start_ = buffer_.capacity() - 1;
  else
    --start_;
  VectorConstruct<T>(
      &buffer_.Buffer()[start_],
      std::forward<Args>(args)...);
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline void Deque<T, InlineCapacity, Allocator>::pop_front() {
  TypeOperations::Destruct(&buffer_.Buffer()[start_],
                           &buffer_.Buffer()[start_ + 1]);
  if (start_ == buffer_.capacity() - 1)
    start_ = 0;
  else
    ++start_;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline void Deque<T, InlineCapacity, Allocator>::pop_back() {
  if (!end_)
    end_ = buffer_.capacity() - 1;
  else
    --end_;
  TypeOperations::Destruct(&buffer_.Buffer()[end_],
                           &buffer_.Buffer()[end_ + 1]);
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline void Deque<T, InlineCapacity, Allocator>::erase(iterator& it) {
  erase(it.index_);
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline void Deque<T, InlineCapacity, Allocator>::erase(const_iterator& it) {
  erase(it.index_);
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline void Deque<T, InlineCapacity, Allocator>::erase(wtf_size_t position) {
  if (position == end_)
    return;

  T* buffer = buffer_.Buffer();
  TypeOperations::Destruct(&buffer[position], &buffer[position + 1]);

  // Find which segment of the circular buffer contained the remove element,
  // and only move elements in that part.
  if (position >= start_) {
    TypeOperations::MoveOverlapping(
        buffer + start_,
        buffer + position,
        buffer + start_ + 1);
    start_ = (start_ + 1) % buffer_.capacity();
  } else {
    TypeOperations::MoveOverlapping(
        buffer + position + 1,
        buffer + end_,
        buffer + position);
    end_ = (end_ - 1 + buffer_.capacity()) % buffer_.capacity();
  }
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline DequeIteratorBase<T, InlineCapacity, Allocator>::DequeIteratorBase(
    const Deque<T, InlineCapacity, Allocator>* deque,
    wtf_size_t index)
    : deque_(const_cast<Deque<T, InlineCapacity, Allocator>*>(deque)),
      index_(index) {
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline DequeIteratorBase<T, InlineCapacity, Allocator>::DequeIteratorBase(
    const DequeIteratorBase& other)
    : deque_(other.deque_),
      index_(other.index_) {
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline DequeIteratorBase<T, InlineCapacity, Allocator>&
DequeIteratorBase<T, InlineCapacity, Allocator>::operator=(
    const DequeIteratorBase<T, 0, Allocator>& other) {
  deque_ = other.deque_;
  index_ = other.index_;
  return *this;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline DequeIteratorBase<T, InlineCapacity, Allocator>::~DequeIteratorBase() =
    default;

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline bool DequeIteratorBase<T, InlineCapacity, Allocator>::IsEqual(
    const DequeIteratorBase& other) const {
  return index_ == other.index_;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline void DequeIteratorBase<T, InlineCapacity, Allocator>::Increment() {
  if (index_ == deque_->buffer_.capacity() - 1)
    index_ = 0;
  else
    ++index_;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline void DequeIteratorBase<T, InlineCapacity, Allocator>::Decrement() {
  if (!index_)
    index_ = deque_->buffer_.capacity() - 1;
  else
    --index_;
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline T* DequeIteratorBase<T, InlineCapacity, Allocator>::After() const {
  return &deque_->buffer_.Buffer()[index_];
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline T* DequeIteratorBase<T, InlineCapacity, Allocator>::Before() const {
  // Port correction: the backing accessor is Buffer(), as in After().
  if (!index_)
    return &deque_->buffer_.Buffer()[deque_->buffer_.capacity() - 1];
  return &deque_->buffer_.Buffer()[index_ - 1];
}

template <typename T, wtf_size_t InlineCapacity, typename Allocator>
inline void swap(Deque<T, InlineCapacity, Allocator>& a,
                 Deque<T, InlineCapacity, Allocator>& b) {
  a.Swap(b);
}

} // namespace bkfont
