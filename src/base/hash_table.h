// Ported from: blink/renderer/platform/wtf/hash_table.h
/*
 * Copyright (C) 2005, 2006, 2007, 2008, 2011, 2012 Apple Inc. All rights
 * reserved.
 * Copyright (C) 2008 David Levin <levin@chromium.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#pragma once

#include <array>
#include <memory>
#include <cstring>
#include <iterator>
#include <ostream>

#include "base/numerics/checked_math.h"
#include "allocator/allocator.h"
#include "allocator/partition_allocator.h"
#include "hash_traits.h"
#include "type_traits.h"
#include "wtf_size_t.h"

namespace bkit {

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
class HashTable;
template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
class HashTableIterator;
template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
class HashTableConstIterator;
typedef enum {
  kHashItemKnownGood
} HashItemKnownGoodTag;

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
class HashTableConstIterator final {

private:
  typedef HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>
      HashTableType;
  typedef HashTableIterator<Key, Value, Extractor, Traits, KeyTraits, Allocator>
      iterator;
  typedef HashTableConstIterator<Key,
                                 Value,
                                 Extractor,
                                 Traits,
                                 KeyTraits,
                                 Allocator>
      const_iterator;
  using value_type = Value;
  typedef typename Traits::IteratorConstGetType GetType;
  typedef const value_type* PointerType;

  friend class HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>;
  friend class HashTableIterator<Key,
                                 Value,
                                 Extractor,
                                 Traits,
                                 KeyTraits,
                                 Allocator>;

  void SkipEmptyBuckets() {
    while (position_ != end_position_ && HashTableType::IsEmptyOrDeletedBucket(*position_))
      ++position_;
  }

  void ReverseSkipEmptyBuckets() {
    // Don't need to check for out-of-bounds positions, as begin position is
    // always going to be a non-empty bucket.
    while (HashTableType::IsEmptyOrDeletedBucket(*position_)) {
      --position_;
    }
  }

  HashTableConstIterator(PointerType position,
                         PointerType begin_position,
                         PointerType end_position,
                         const HashTableType* container)
      : position_(position),
        end_position_(end_position) {
    SkipEmptyBuckets();
  }

  HashTableConstIterator(PointerType position,
                         PointerType begin_position,
                         PointerType end_position,
                         const HashTableType* container,
                         HashItemKnownGoodTag)
      : position_(position),
        end_position_(end_position) {
  }

public:
  constexpr HashTableConstIterator() = default;

  GetType Get() const {
    return position_;
  }
  typename Traits::IteratorConstReferenceType operator*() const {
    return *Get();
  }
  GetType operator->() const {
    return Get();
  }

  const_iterator& operator++() {

    ++position_;
    SkipEmptyBuckets();
    return *this;
  }

  const_iterator operator++(int) {
    // The source spells this as `this`; postfix increment returns an iterator.
    auto copy = *this;
    ++(*this);
    return copy;
  }

  const_iterator& operator--() {
    --position_;
    ReverseSkipEmptyBuckets();
    return *this;
  }

  const_iterator operator--(int) {
    auto copy = *this;
    --(*this);
    return copy;
  }

  // Comparison.
  bool operator==(const const_iterator& other) const {
    return position_ == other.position_;
  }
  bool operator!=(const const_iterator& other) const {
    return position_ != other.position_;
  }
  bool operator==(const iterator& other) const {
    return *this == static_cast<const_iterator>(other);
  }
  bool operator!=(const iterator& other) const {
    return *this != static_cast<const_iterator>(other);
  }

  std::ostream& PrintTo(std::ostream& stream) const {
    if (position_ == end_position_)
      return stream << "iterator representing <end>";
    // TODO(tkent): Change |position_| to |*position_| to show the
    // pointed object. It requires a lot of new stream printer functions.
    return stream << "iterator pointing to " << position_;
  }

private:
  PointerType position_ = nullptr;
  PointerType end_position_ = nullptr;
};

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
std::ostream& operator<<(std::ostream& stream,
                         const HashTableConstIterator<Key,
                                                      Value,
                                                      Extractor,
                                                      Traits,
                                                      KeyTraits,
                                                      Allocator>& iterator) {
  return iterator.PrintTo(stream);
}

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
class HashTableIterator final {

private:
  typedef HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>
      HashTableType;
  typedef HashTableIterator<Key, Value, Extractor, Traits, KeyTraits, Allocator>
      iterator;
  typedef HashTableConstIterator<Key,
                                 Value,
                                 Extractor,
                                 Traits,
                                 KeyTraits,
                                 Allocator>
      const_iterator;
  using value_type = Value;
  typedef typename Traits::IteratorGetType GetType;
  typedef value_type* PointerType;

  friend class HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>;

  HashTableIterator(PointerType pos,
                    PointerType begin,
                    PointerType end,
                    const HashTableType* container)
      : iterator_(pos, begin, end, container) {
  }
  HashTableIterator(PointerType pos,
                    PointerType begin,
                    PointerType end,
                    const HashTableType* container,
                    HashItemKnownGoodTag tag)
      : iterator_(pos, begin, end, container, tag) {
  }

public:
  constexpr HashTableIterator() = default;

  // default copy, assignment and destructor are OK

  GetType Get() const {
    return const_cast<GetType>(iterator_.Get());
  }
  typename Traits::IteratorReferenceType operator*() const {
    return *Get();
  }
  GetType operator->() const {
    return Get();
  }

  iterator& operator++() {
    ++iterator_;
    return *this;
  }

  iterator operator++(int) {
    auto copy = *this;
    ++(*this);
    return copy;
  }

  iterator& operator--() {
    --iterator_;
    return *this;
  }

  iterator operator--(int) {
    auto copy = *this;
    --(*this);
    return copy;
  }

  // Comparison.
  bool operator==(const iterator& other) const {
    return iterator_ == other.iterator_;
  }
  bool operator!=(const iterator& other) const {
    return iterator_ != other.iterator_;
  }
  bool operator==(const const_iterator& other) const {
    return iterator_ == other;
  }
  bool operator!=(const const_iterator& other) const {
    return iterator_ != other;
  }

  operator const_iterator() const {
    return iterator_;
  }
  std::ostream& PrintTo(std::ostream& stream) const {
    return iterator_.PrintTo(stream);
  }

private:
  const_iterator iterator_;
};

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
std::ostream& operator<<(std::ostream& stream,
                         const HashTableIterator<Key,
                                                 Value,
                                                 Extractor,
                                                 Traits,
                                                 KeyTraits,
                                                 Allocator>& iterator) {
  return iterator.PrintTo(stream);
}

template <typename KeyTraits>
class IdentityHashTranslator {
  STATIC_ONLY(IdentityHashTranslator);

public:
  template <typename T>
  static unsigned GetHash(const T& key) {
    return KeyTraits::GetHash(key);
  }
  template <typename T, typename U>
  static bool Equal(const T& a, const U& b) {
    return KeyTraits::Equal(a, b);
  }
  template <typename T, typename U, typename V>
  static void Store(T& location, U&&, V&& value) {
    location = std::forward<V>(value);
  }
};

template <typename HashTableType, typename ValueType>
struct HashTableAddResult final {

public:
  HashTableAddResult([[maybe_unused]] const HashTableType* container,
                     ValueType* stored_value,
                     bool is_new_entry)
      : stored_value(stored_value),
        is_new_entry(is_new_entry) {
  }

  ValueType* stored_value;
  bool is_new_entry;
};

// Note: empty or deleted key values are not allowed, using them may lead to
// undefined behavior.  For pointer keys this means that null pointers are not
// allowed unless you supply custom key traits.
template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
class HashTable final {
  DISALLOW_NEW();

public:
  typedef HashTableIterator<Key, Value, Extractor, Traits, KeyTraits, Allocator>
      iterator;
  typedef HashTableConstIterator<Key,
                                 Value,
                                 Extractor,
                                 Traits,
                                 KeyTraits,
                                 Allocator>
      const_iterator;
  typedef Traits ValueTraits;
  typedef Key KeyType;
  typedef typename KeyTraits::PeekInType KeyPeekInType;
  typedef Value ValueType;
  typedef Extractor ExtractorType;
  typedef KeyTraits KeyTraitsType;
  typedef IdentityHashTranslator<KeyTraits> IdentityTranslatorType;
  typedef HashTableAddResult<HashTable, ValueType> AddResult;

  HashTable();

  ~HashTable() {
    if (!table_) [[likely]] {
      return;
    }
    DeleteAllBucketsAndDeallocate(table_, table_size_);
    table_ = nullptr;
  }

  HashTable(const HashTable&);
  HashTable(HashTable&&);
  void swap(HashTable&);
  HashTable& operator=(const HashTable&);
  HashTable& operator=(HashTable&&);

  // When the hash table is empty, just return the same iterator for end as
  // for begin.  This is more efficient because we don't have to skip all the
  // empty and deleted buckets, and iterating an empty table is a common case
  // that's worth optimizing.
  iterator begin() {
    return empty() ? end() : MakeIterator(table_);
  }
  iterator end() {
    return MakeKnownGoodIterator(table_ + table_size_);
  }
  const_iterator begin() const {
    return empty() ? end() : MakeConstIterator(table_);
  }
  const_iterator end() const {
    return MakeKnownGoodConstIterator(table_ + table_size_);
  }

  wtf_size_t size() const {

    return key_count_;
  }
  wtf_size_t Capacity() const {

    return table_size_;
  }
  bool empty() const {

    return !key_count_;
  }

  void ReserveCapacityForSize(wtf_size_t size);

  template <typename IncomingValueType>
  AddResult insert(IncomingValueType&& value) {
    return insert<IdentityTranslatorType>(
        Extractor::ExtractKey(value),
        std::forward<IncomingValueType>(value));
  }

  // A special version of insert() that finds the object by hashing and
  // comparing with some other type, to avoid the cost of type conversion if the
  // object is already in the table.
  // HashTranslator must have the following function members:
  //   static unsigned GetHash(const T&);
  //   static bool Equal(const ValueType&, const T&);
  //   static void Store(T& location, KeyType&&, ValueType&&);
  template <typename HashTranslator, typename T, typename Extra>
  AddResult insert(T&& key, Extra&&);
  // Similar to the above, but passes additional `unsigned hash_code`, which
  // is computed from `HashTranslator::GetHash(key)`, to HashTranslator method
  //   static Store(T&, KeyType&&, ValueType&&, unsigned hash_code);
  // to avoid recomputation of the hash code when needed in the method.
  template <typename HashTranslator, typename T, typename Extra>
  AddResult InsertPassingHashCode(T&& key, Extra&&);

  iterator find(KeyPeekInType key) {
    return Find<IdentityTranslatorType>(key);
  }
  const_iterator find(KeyPeekInType key) const {
    return Find<IdentityTranslatorType>(key);
  }
  bool Contains(KeyPeekInType key) const {
    return Contains<IdentityTranslatorType>(key);
  }

  // A special version of find() that finds the object by hashing and
  // comparing with some other type, to avoid the cost of type conversion.
  // HashTranslator must have the following function members:
  //   static unsigned GetHash(const T&);
  //   static bool Equal(const ValueType&, const T&);
  template <typename HashTranslator, typename T>
  iterator Find(const T&);
  template <typename HashTranslator, typename T>
  const_iterator Find(const T&) const;
  template <typename HashTranslator, typename T>
  bool Contains(const T&) const;

  void erase(KeyPeekInType);
  void erase(iterator);
  void erase(const_iterator);
  template <typename Pred>
  void erase_if(Pred pred);

  void clear();

  static bool IsEmptyBucket(const ValueType& value) {
    return IsHashTraitsEmptyValue<KeyTraits>(Extractor::ExtractKey(value));
  }
  static bool IsDeletedBucket(const ValueType& value) {
    return IsHashTraitsDeletedValue<KeyTraits>(Extractor::ExtractKey(value));
  }
  static bool IsEmptyOrDeletedBucket(const ValueType& value) {
    return IsHashTraitsEmptyOrDeletedValue<KeyTraits>(
        Extractor::ExtractKey(value));
  }

  ValueType* Lookup(KeyPeekInType key) {
    return Lookup<IdentityTranslatorType, KeyPeekInType>(key);
  }
  const ValueType* Lookup(KeyPeekInType key) const {
    return Lookup<IdentityTranslatorType, KeyPeekInType>(key);
  }
  template <typename HashTranslator, typename T>
  ValueType* Lookup(const T&);
  template <typename HashTranslator, typename T>
  const ValueType* Lookup(const T&) const;

private:
  static ValueType* AllocateTable(wtf_size_t size);
  static void DeleteAllBucketsAndDeallocate(ValueType* table, wtf_size_t size);

  struct LookupResult {
    ValueType* entry;
    bool found;
    unsigned hash;
  };
  template <typename HashTranslator, typename T>
  LookupResult LookupForWriting(const T&);

  void erase(const ValueType*);

  bool ShouldExpand() const {
    return (key_count_ + deleted_count_) * kMaxLoad >= table_size_;
  }
  bool MustRehashInPlace() const {
    return key_count_ * kMinLoad < table_size_ * 2;
  }
  bool ShouldShrink() const {
    return key_count_ * kMinLoad < table_size_ && table_size_ > KeyTraits::kMinimumTableSize;
  }
  ValueType* Expand(ValueType* entry = nullptr);
  void Shrink() {
    Rehash(table_size_ / 2, nullptr);
  }

  ValueType* RehashTo(ValueType* new_table,
                      wtf_size_t new_table_size,
                      ValueType* entry);
  ValueType* Rehash(wtf_size_t new_table_size, ValueType* entry);
  ValueType* Reinsert(ValueType&&);

  static void ReinitializeBucket(ValueType& bucket);
  static void DeleteBucket(ValueType& bucket) {
    bucket.~ValueType();
    ConstructHashTraitsDeletedValue<KeyTraits>(Extractor::ExtractKey(bucket));
  }

  iterator MakeIterator(ValueType* pos) {
    return iterator(pos, table_, table_ + table_size_, this);
  }
  const_iterator MakeConstIterator(const ValueType* pos) const {
    return const_iterator(pos, table_, table_ + table_size_, this);
  }
  iterator MakeKnownGoodIterator(ValueType* pos) {
    return iterator(pos, table_, table_ + table_size_, this, kHashItemKnownGood);
  }
  const_iterator MakeKnownGoodConstIterator(const ValueType* pos) const {
    return const_iterator(pos, table_, table_ + table_size_, this, kHashItemKnownGood);
  }

  static const unsigned kMaxLoad = 2;
  static const unsigned kMinLoad = 6;

  unsigned TableSizeMask() const {
    unsigned mask = table_size_ - 1;

    return mask;
  }

  // Constructor for hash tables with raw storage.
  struct RawStorageTag {};
  HashTable(RawStorageTag, ValueType* table, wtf_size_t size)
      : table_(table),
        table_size_(size) {
  }

  ValueType* table_;
  wtf_size_t table_size_;
  wtf_size_t key_count_ = 0;
  wtf_size_t deleted_count_ : 31 = 0;
};

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
inline HashTable<Key,
                 Value,
                 Extractor,

                 Traits,
                 KeyTraits,
                 Allocator>::HashTable()
    : table_(nullptr),
      table_size_(0),
      key_count_(0),
      deleted_count_(0) {
}

inline wtf_size_t CalculateCapacity(wtf_size_t size) {
  for (wtf_size_t mask = size; mask; mask >>= 1) {
    size |= mask; // 00110101010 -> 00111111111
  }
  return (size + 1) * 2; // 00111111111 -> 10000000000
}

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
void HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::
    ReserveCapacityForSize(wtf_size_t new_size) {
  wtf_size_t new_capacity = CalculateCapacity(new_size);
  if (new_capacity < KeyTraits::kMinimumTableSize)
    new_capacity = KeyTraits::kMinimumTableSize;

  if (new_capacity > Capacity()) {
    // HashTable capacity should not overflow 32bit int.
    Rehash(new_capacity, nullptr);
  }
}

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
template <typename HashTranslator, typename T>
inline Value*
HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::Lookup(
    const T& key) {
  // Call the const version of Lookup<HashTranslator, T>().
  return const_cast<Value*>(
      const_cast<const HashTable*>(this)->Lookup<HashTranslator>(key));
}

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
template <typename HashTranslator, typename T>
inline const Value*
HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::Lookup(
    const T& key) const {

  const ValueType* table = table_;
  if (!table)
    return nullptr;

  size_t size_mask = TableSizeMask();
  unsigned h = HashTranslator::GetHash(key);
  size_t i = h & size_mask;
  size_t probe_count = 0;

  while (true) {
    const ValueType* entry = table + i;

    if (KeyTraits::kSafeToCompareToEmptyOrDeleted) {
      if (HashTranslator::Equal(Extractor::ExtractKey(*entry), key)) {
        return entry;
      }

      if (IsEmptyBucket(*entry))
        return nullptr;
    } else {
      if (IsEmptyBucket(*entry))
        return nullptr;

      if (!IsDeletedBucket(*entry) && HashTranslator::Equal(Extractor::ExtractKey(*entry), key)) {
        return entry;
      }
    }
    ++probe_count;
    i = (i + probe_count) & size_mask;
  }
}

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
template <typename HashTranslator, typename T>
inline typename HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::
    LookupResult
    HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::
        LookupForWriting(const T& key) {

  ValueType* table = table_;
  size_t size_mask = TableSizeMask();
  unsigned h = HashTranslator::GetHash(key);
  size_t i = h & size_mask;
  size_t probe_count = 0;

  ValueType* deleted_entry = nullptr;

  while (true) {
    ValueType* entry = table + i;

    if (IsEmptyBucket(*entry))
      return LookupResult{deleted_entry ? deleted_entry : entry, false, h};

    if (KeyTraits::kSafeToCompareToEmptyOrDeleted) {
      if (HashTranslator::Equal(Extractor::ExtractKey(*entry), key)) {
        return LookupResult{entry, true, h};
      }

      if (IsDeletedBucket(*entry)) {
        deleted_entry = entry;
      }
    } else {
      if (IsDeletedBucket(*entry)) {
        deleted_entry = entry;
      } else if (HashTranslator::Equal(Extractor::ExtractKey(*entry), key)) {
        return LookupResult{entry, true, h};
      }
    }
    ++probe_count;
    i = (i + probe_count) & size_mask;
  }
}

template <typename Traits,
          typename Allocator,
          typename Value,
          bool = Traits::kEmptyValueIsZero>
struct HashTableBucketInitializer {
  STATIC_ONLY(HashTableBucketInitializer);

  static_assert(!Traits::kEmptyValueIsZero);

  static void Reinitialize(Value& bucket) {
    new (&bucket) Value(Traits::EmptyValue());
  }

  template <typename HashTable>
  static Value* AllocateTable(wtf_size_t size, size_t alloc_size) {
    Value* result =
        Allocator::template AllocateHashTableBacking<Value, HashTable>(
            alloc_size);
    InitializeTable(result, size);
    return result;
  }

  static void InitializeTable(Value* table, wtf_size_t size) {
    for (wtf_size_t i = 0; i < size; i++) {
      Reinitialize(table[i]);
    }
  }
};

// Specialization when the hash traits for a type have kEmptyValueIsZero = true
// which indicate that all zero bytes represent an empty object.
template <typename Traits, typename Allocator, typename Value>
struct HashTableBucketInitializer<Traits, Allocator, Value, true> {
  STATIC_ONLY(HashTableBucketInitializer);

  static void Reinitialize(Value& bucket) {
    memset(&bucket, 0, sizeof(bucket));
  }

  template <typename HashTable>
  static Value* AllocateTable(wtf_size_t size, size_t alloc_size) {
    Value* result =
        Allocator::template AllocateZeroedHashTableBacking<Value, HashTable>(
            alloc_size);
    return result;
  }

  static void InitializeTable(Value* table, wtf_size_t size) {
    memset(table, 0, size * sizeof(Value));
  }
};

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
inline void HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::
    ReinitializeBucket(ValueType& bucket) {
  // Reinitialize is used when recycling a deleted bucket.

  HashTableBucketInitializer<Traits, Allocator, Value>::Reinitialize(bucket);
}

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
template <typename HashTranslator, typename T, typename Extra>
typename HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::
    AddResult
    HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::insert(
        T&& key,
        Extra&& extra) {

  if (!table_)
    Expand();

  ValueType* table = table_;
  size_t size_mask = TableSizeMask();
  unsigned h = HashTranslator::GetHash(key);
  size_t i = h & size_mask;
  size_t probe_count = 0;

  ValueType* deleted_entry = nullptr;
  ValueType* entry;
  while (true) {
    entry = table + i;

    if (IsEmptyBucket(*entry))
      break;

    if (KeyTraits::kSafeToCompareToEmptyOrDeleted) {
      if (HashTranslator::Equal(Extractor::ExtractKey(*entry), key)) {
        return AddResult(this, entry, false);
      }

      if (IsDeletedBucket(*entry)) {
        deleted_entry = entry;
      }
    } else {
      if (IsDeletedBucket(*entry)) {
        deleted_entry = entry;
      } else if (HashTranslator::Equal(Extractor::ExtractKey(*entry), key)) {
        return AddResult(this, entry, false);
      }
    }
    ++probe_count;
    i = (i + probe_count) & size_mask;
  }

  if (deleted_entry) {

    // Overwrite any data left over from last use, using placement new or
    // memset.
    ReinitializeBucket(*deleted_entry);
    entry = deleted_entry;
    --deleted_count_;
  }

  HashTranslator::Store(*entry, std::forward<T>(key), std::forward<Extra>(extra));

  ++key_count_;

  if (ShouldExpand()) {
    entry = Expand(entry);
  }

  return AddResult(this, entry, true);
}

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
template <typename HashTranslator, typename T, typename Extra>
typename HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::
    AddResult
    HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::
        InsertPassingHashCode(T&& key, Extra&& extra) {

  if (!table_)
    Expand();

  LookupResult lookup_result = LookupForWriting<HashTranslator>(key);
  ValueType* entry = lookup_result.entry;
  if (lookup_result.found) {
    return AddResult(this, entry, false);
  }

  if (IsDeletedBucket(*entry)) {
    ReinitializeBucket(*entry);
    --deleted_count_;
  }

  HashTranslator::Store(*entry, std::forward<T>(key), std::forward<Extra>(extra), lookup_result.hash);

  ++key_count_;
  if (ShouldExpand())
    entry = Expand(entry);

  return AddResult(this, entry, true);
}

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
Value* HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::Reinsert(
    ValueType&& entry) {

  ValueType* table = table_;
  size_t size_mask = TableSizeMask();
  const auto& key = Extractor::ExtractKey(entry);
  unsigned h = KeyTraits::GetHash(key);
  size_t i = h & size_mask;
  size_t probe_count = 0;

  ValueType* new_entry = table + i;
  while (!IsEmptyBucket(*new_entry)) {

    ++probe_count;
    i = (i + probe_count) & size_mask;
    new_entry = table + i;
  }

  new_entry->~ValueType();
  new (new_entry) ValueType(std::move(entry));

  return new_entry;
}

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
template <typename HashTranslator, typename T>
inline typename HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::
    iterator
    HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::Find(
        const T& key) {
  ValueType* entry = Lookup<HashTranslator>(key);
  if (!entry)
    return end();

  return MakeKnownGoodIterator(entry);
}

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
template <typename HashTranslator, typename T>
inline typename HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::
    const_iterator
    HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::Find(
        const T& key) const {
  const ValueType* entry = Lookup<HashTranslator>(key);
  if (!entry)
    return end();

  return MakeKnownGoodConstIterator(entry);
}

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
template <typename HashTranslator, typename T>
bool HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::Contains(
    const T& key) const {
  return Lookup<HashTranslator>(key);
}

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
void HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::erase(
    const ValueType* pos) {

  DeleteBucket(const_cast<ValueType&>(*pos));
  ++deleted_count_;
  --key_count_;

  if (ShouldShrink())
    Shrink();
}

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
template <typename Pred>
void HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::erase_if(
    Pred pred) {

  for (wtf_size_t i = 0; i < table_size_; ++i) {
    if (!IsEmptyOrDeletedBucket(table_[i]) && pred(table_[i])) {
      DeleteBucket(table_[i]);
      ++deleted_count_;
      --key_count_;
    }
  }

  if (ShouldShrink()) {
    Shrink();
  }
}

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
inline void
HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::erase(
    iterator it) {
  if (it == end())
    return;
  erase(it.iterator_.position_);
}

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
inline void
HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::erase(
    const_iterator it) {
  if (it == end())
    return;
  erase(it.position_);
}

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
inline void
HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::erase(
    KeyPeekInType key) {
  erase(find(key));
}

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
Value*
HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::AllocateTable(
    wtf_size_t size) {
  // Assert that we will not use memset on things with a vtable entry.  The
  // compiler will also check this on some platforms. We would like to check
  // this on the whole value (key-value pair), but std::is_polymorphic will
  // return false for a pair of two types, even if one of the components is
  // polymorphic.
  static_assert(
      !Traits::kEmptyValueIsZero || !std::is_polymorphic<KeyType>::value,
      "empty value cannot be zero for things with a vtable");

  size_t alloc_size = base::CheckMul(size, sizeof(ValueType)).ValueOrDie();
  return HashTableBucketInitializer<
      Traits,
      Allocator,
      Value>::template AllocateTable<HashTable>(size,
                                                alloc_size);
}

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
void HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::
    DeleteAllBucketsAndDeallocate(ValueType* table, wtf_size_t size) {
  if constexpr (!std::is_trivially_destructible_v<ValueType>) {
    for (wtf_size_t i = 0; i < size; ++i) {
      if (!IsDeletedBucket(table[i]))
        table[i].~ValueType();
    }
  }
  Allocator::template FreeHashTableBacking<ValueType, HashTable>(table);
}

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
Value* HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::Expand(
    Value* entry) {
  wtf_size_t new_size;
  if (!table_size_) {
    new_size = KeyTraits::kMinimumTableSize;
  } else if (MustRehashInPlace()) {
    new_size = table_size_;
  } else {
    new_size = table_size_ * 2;
  }

  return Rehash(new_size, entry);
}

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
Value* HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::RehashTo(
    ValueType* new_table,
    wtf_size_t new_table_size,
    Value* entry) {

  HashTable new_hash_table(RawStorageTag{}, new_table, new_table_size);

  Value* new_entry = nullptr;
  for (wtf_size_t i = 0; i != table_size_; ++i) {
    if (IsEmptyOrDeletedBucket(table_[i])) {

      continue;
    }
    Value* reinserted_entry = new_hash_table.Reinsert(std::move(table_[i]));
    if (&table_[i] == entry) {

      new_entry = reinserted_entry;
    }
  }

  ValueType* old_table = table_;
  wtf_size_t old_table_size = table_size_;

  table_ = new_hash_table.table_;
  table_size_ = new_table_size;

  new_hash_table.table_ = old_table;
  new_hash_table.table_size_ = old_table_size;

  new_hash_table.clear();

  deleted_count_ = 0;

  return new_entry;
}

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
Value* HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::Rehash(
    wtf_size_t new_table_size,
    Value* entry) {
  ValueType* new_table = AllocateTable(new_table_size);
  Value* new_entry = RehashTo(new_table, new_table_size, entry);

  return new_entry;
}

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
void HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::clear() {
  if (!table_)
    return;

  DeleteAllBucketsAndDeallocate(table_, table_size_);
  table_ = nullptr;
  table_size_ = 0;
  key_count_ = 0;
}

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::HashTable(
    const HashTable& other)
    : table_(nullptr),
      table_size_(0),
      key_count_(0),
      deleted_count_(0) {

  table_size_ = other.table_size_;
  if (table_size_ == 0) {
    return;
  }
  table_ = AllocateTable(table_size_);
  key_count_ = other.key_count_;
  deleted_count_ = other.deleted_count_;

  for (wtf_size_t i = 0; i < table_size_; i++) {
    if (other.IsEmptyBucket(other.table_[i])) {
      // Do nothing. All entries are initially empty by AllocateTable().
    } else if (other.IsDeletedBucket(other.table_[i])) {
      ConstructHashTraitsDeletedValue<KeyTraits>(
          Extractor::ExtractKey(table_[i]));
    } else {
      new (&table_[i]) ValueType(other.table_[i]);
    }
  }
}

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::HashTable(
    HashTable&& other)
    : table_(nullptr),
      table_size_(0),
      key_count_(0),
      deleted_count_(0) {
  swap(other);
}

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
void HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::swap(
    HashTable& other) {

  std::swap(table_, other.table_);
  std::swap(table_size_, other.table_size_);
  std::swap(key_count_, other.key_count_);
  // std::swap does not work for bit fields.
  wtf_size_t deleted = deleted_count_;
  deleted_count_ = other.deleted_count_;
  other.deleted_count_ = deleted;
}

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>&
HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::operator=(
    const HashTable& other) {
  HashTable tmp(other);
  swap(tmp);
  return *this;
}

template <typename Key,
          typename Value,
          typename Extractor,
          typename Traits,
          typename KeyTraits,
          typename Allocator>
HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>&
HashTable<Key, Value, Extractor, Traits, KeyTraits, Allocator>::operator=(
    HashTable&& other) {
  swap(other);
  return *this;
}

// iterator adapters

template <typename HashTableType, typename Traits>
struct HashTableConstIteratorAdapter {

  using iterator_category = std::bidirectional_iterator_tag;
  using value_type = HashTableType::ValueType;
  using difference_type = ptrdiff_t;
  using pointer = value_type*;
  using reference = value_type&;

  HashTableConstIteratorAdapter() = default;
  HashTableConstIteratorAdapter(
      const typename HashTableType::const_iterator& impl)
      : impl_(impl) {
  }
  typedef typename Traits::IteratorConstGetType GetType;
  typedef
      typename HashTableType::ValueTraits::IteratorConstGetType SourceGetType;

  GetType Get() const {
    return const_cast<GetType>(SourceGetType(impl_.Get()));
  }
  typename Traits::IteratorConstReferenceType operator*() const {
    return *Get();
  }
  GetType operator->() const {
    return Get();
  }

  HashTableConstIteratorAdapter& operator++() {
    ++impl_;
    return *this;
  }
  HashTableConstIteratorAdapter operator++(int) {
    HashTableConstIteratorAdapter copy = *this;
    ++*this;
    return copy;
  }
  HashTableConstIteratorAdapter& operator--() {
    --impl_;
    return *this;
  }
  HashTableConstIteratorAdapter operator--(int) {
    HashTableConstIteratorAdapter copy = *this;
    --*this;
    return copy;
  }
  typename HashTableType::const_iterator impl_;
};

template <typename HashTable, typename Traits>
std::ostream& operator<<(
    std::ostream& stream,
    const HashTableConstIteratorAdapter<HashTable, Traits>& iterator) {
  return stream << iterator.impl_;
}

template <typename HashTableType, typename Traits>
struct HashTableIteratorAdapter {

  using iterator_category = std::bidirectional_iterator_tag;
  using value_type = HashTableType::ValueType;
  using difference_type = ptrdiff_t;
  using pointer = value_type*;
  using reference = value_type&;

  typedef typename Traits::IteratorGetType GetType;
  typedef typename HashTableType::ValueTraits::IteratorGetType SourceGetType;

  constexpr HashTableIteratorAdapter() = default;
  HashTableIteratorAdapter(const typename HashTableType::iterator& impl)
      : impl_(impl) {
  }

  GetType Get() const {
    // Use the underlying iterator's Get() spelling.
    return const_cast<GetType>(SourceGetType(impl_.Get()));
  }
  typename Traits::IteratorReferenceType operator*() const {
    return *Get();
  }
  GetType operator->() const {
    return Get();
  }

  HashTableIteratorAdapter& operator++() {
    ++impl_;
    return *this;
  }
  HashTableIteratorAdapter operator++(int) {
    auto copy = *this;
    ++(*this);
    return copy;
  }

  HashTableIteratorAdapter& operator--() {
    --impl_;
    return *this;
  }
  HashTableIteratorAdapter operator--(int) {
    auto copy = *this;
    --(*this);
    return copy;
  }

  operator HashTableConstIteratorAdapter<HashTableType, Traits>() {
    typename HashTableType::const_iterator i = impl_;
    return i;
  }

  typename HashTableType::iterator impl_;
};

template <typename HashTable, typename Traits>
std::ostream& operator<<(
    std::ostream& stream,
    const HashTableIteratorAdapter<HashTable, Traits>& iterator) {
  return stream << iterator.impl_;
}

template <typename T, typename U>
inline bool operator==(const HashTableConstIteratorAdapter<T, U>& a,
                       const HashTableConstIteratorAdapter<T, U>& b) {
  return a.impl_ == b.impl_;
}

template <typename T, typename U>
inline bool operator!=(const HashTableConstIteratorAdapter<T, U>& a,
                       const HashTableConstIteratorAdapter<T, U>& b) {
  return a.impl_ != b.impl_;
}

template <typename T, typename U>
inline bool operator==(const HashTableIteratorAdapter<T, U>& a,
                       const HashTableIteratorAdapter<T, U>& b) {
  return a.impl_ == b.impl_;
}

template <typename T, typename U>
inline bool operator!=(const HashTableIteratorAdapter<T, U>& a,
                       const HashTableIteratorAdapter<T, U>& b) {
  return a.impl_ != b.impl_;
}

// All 4 combinations of ==, != and Const,non const.
template <typename T, typename U>
inline bool operator==(const HashTableConstIteratorAdapter<T, U>& a,
                       const HashTableIteratorAdapter<T, U>& b) {
  return a.impl_ == b.impl_;
}

template <typename T, typename U>
inline bool operator!=(const HashTableConstIteratorAdapter<T, U>& a,
                       const HashTableIteratorAdapter<T, U>& b) {
  return a.impl_ != b.impl_;
}

template <typename T, typename U>
inline bool operator==(const HashTableIteratorAdapter<T, U>& a,
                       const HashTableConstIteratorAdapter<T, U>& b) {
  return a.impl_ == b.impl_;
}

template <typename T, typename U>
inline bool operator!=(const HashTableIteratorAdapter<T, U>& a,
                       const HashTableConstIteratorAdapter<T, U>& b) {
  return a.impl_ != b.impl_;
}

template <typename Collection1, typename Collection2>
inline void RemoveAll(Collection1& collection,
                      const Collection2& to_be_removed) {
  if (collection.empty() || to_be_removed.empty())
    return;
  typedef typename Collection2::const_iterator CollectionIterator;
  CollectionIterator end(to_be_removed.end());
  for (CollectionIterator it(to_be_removed.begin()); it != end; ++it)
    collection.erase(*it);
}

} // namespace bkit
