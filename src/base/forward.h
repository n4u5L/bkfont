/*
 *  Copyright (C) 2006, 2009, 2011 Apple Inc. All rights reserved.
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

// Container declarations derived from Chromium's Blink platform/wtf headers.
#pragma once

#include <cstdint>
#include <limits>

namespace bkfont {

template <typename T>
class scoped_refptr;
using wtf_size_t = std::uint32_t;
inline constexpr wtf_size_t kNotFound = std::numeric_limits<wtf_size_t>::max();
class PartitionAllocator;
class String;
class StringImpl;
class StringView;
class StringBuilder;
class AtomicString;
template <typename T>
class StringBuffer;
template <typename T>
struct HashTraits;
template <typename T, wtf_size_t InlineCapacity = 0,
          typename Allocator = PartitionAllocator>
class Vector;
template <typename T, wtf_size_t InlineCapacity = 0,
          typename Allocator = PartitionAllocator>
class Deque;
template <typename Key, typename Mapped, typename KeyTraits = HashTraits<Key>,
          typename MappedTraits = HashTraits<Mapped>,
          typename Allocator = PartitionAllocator>
class HashMap;
template <typename Value, typename Traits = HashTraits<Value>,
          typename Allocator = PartitionAllocator>
class HashSet;
template <typename Value, typename Traits = HashTraits<Value>,
          typename Allocator = PartitionAllocator>
class LinkedHashSet;
} // namespace bkfont
