/*
 * Copyright (C) 2006, 2007, 2008 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB. If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
// Source: third_party/blink/renderer/platform/wtf/vector_traits.h
#pragma once

#include <memory>
#include <type_traits>
#include <utility>
#include "base/memory/scoped_refptr.h"

namespace bkfont {

// Original bulk-operation traits. Collection tracing and clearing unused GC
// slots are absent; ordinary object construction and destruction remain.
template <typename T>
struct VectorTraitsBase {
  using TraitType = T;
  static const bool kNeedsDestruction = !std::is_trivially_destructible<T>::value;
  static constexpr bool kCanInitializeWithMemset =
      std::is_trivially_default_constructible<T>::value;
  static constexpr bool kCanFillWithMemset =
      std::is_default_constructible<T>::value && (sizeof(T) == sizeof(char));
  static constexpr bool kCanCompareWithMemcmp = std::is_scalar<T>::value;
  static constexpr bool kCanMoveWithMemcpy =
      std::is_trivially_move_assignable<T>::value;
  static constexpr bool kCanCopyWithMemcpy =
      std::is_trivially_copy_assignable<T>::value;
};

template <typename T>
struct VectorTraits : VectorTraitsBase<T> {};

template <typename T>
struct SimpleClassVectorTraits : VectorTraitsBase<T> {
  static const bool kCanInitializeWithMemset = true;
  static const bool kCanMoveWithMemcpy = true;
  static const bool kCanCompareWithMemcmp = true;
};

template <typename P>
struct VectorTraits<scoped_refptr<P>>
    : SimpleClassVectorTraits<scoped_refptr<P>> {
  static const bool kCanCopyWithMemcpy = false;
};

template <typename P>
struct VectorTraits<std::unique_ptr<P>>
    : SimpleClassVectorTraits<std::unique_ptr<P>> {
  static const bool kCanCopyWithMemcpy = false;
};

template <typename First, typename Second>
struct VectorTraits<std::pair<First, Second>> {
  using TraitType = std::pair<First, Second>;
  using FirstTraits = VectorTraits<First>;
  using SecondTraits = VectorTraits<Second>;
  static const bool kNeedsDestruction =
      FirstTraits::kNeedsDestruction || SecondTraits::kNeedsDestruction;
  static const bool kCanInitializeWithMemset =
      FirstTraits::kCanInitializeWithMemset && SecondTraits::kCanInitializeWithMemset;
  static const bool kCanMoveWithMemcpy =
      FirstTraits::kCanMoveWithMemcpy && SecondTraits::kCanMoveWithMemcpy;
  static const bool kCanCopyWithMemcpy =
      FirstTraits::kCanCopyWithMemcpy && SecondTraits::kCanCopyWithMemcpy;
  static const bool kCanFillWithMemset = false;
  static const bool kCanCompareWithMemcmp =
      FirstTraits::kCanCompareWithMemcmp && SecondTraits::kCanCompareWithMemcmp;
};

} // namespace bkfont

#define BASE_ALLOW_MOVE_INIT_AND_COMPARE_WITH_MEM_FUNCTIONS(ClassName)    \
  namespace bkfont {                                                       \
  template <>                                                             \
  struct VectorTraits<ClassName> : SimpleClassVectorTraits<ClassName> {}; \
  }

#define BASE_ALLOW_MOVE_AND_INIT_WITH_MEM_FUNCTIONS(ClassName)   \
  namespace bkfont {                                              \
  template <>                                                    \
  struct VectorTraits<ClassName> : VectorTraitsBase<ClassName> { \
    static const bool kCanInitializeWithMemset = true;           \
    static const bool kCanMoveWithMemcpy = true;                 \
  };                                                             \
  }

#define BASE_ALLOW_INIT_WITH_MEM_FUNCTIONS(ClassName)            \
  namespace bkfont {                                              \
  template <>                                                    \
  struct VectorTraits<ClassName> : VectorTraitsBase<ClassName> { \
    static const bool kCanInitializeWithMemset = true;           \
  };                                                             \
  }
