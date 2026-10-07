// Ported from: blink/renderer/platform/wtf/type_traits.h
/*
 * Copyright (C) 2006, 2007, 2008 Apple Inc. All rights reserved.
 * Copyright (C) 2009, 2010 Google Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#pragma once

#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include "base/compiler_specific.h"
#include "build/build_config.h"

namespace bkit {

// Returns a string that contains the type name of |T| as a substring.
template <typename T>
inline const char* GetStringWithTypeName() {
  return PRETTY_FUNCTION;
}

template <typename T, typename U>
struct IsSubclass {
private:
  typedef char YesType;
  struct NoType {
    char padding[8];
  };

  static YesType SubclassCheck(U*);
  static NoType SubclassCheck(...);
  static T* t_;

public:
  static const bool value = sizeof(SubclassCheck(t_)) == sizeof(YesType);
};

template <typename T, template <typename... V> class U>
struct IsSubclassOfTemplate {
private:
  typedef char YesType;
  struct NoType {
    char padding[8];
  };

  template <typename... W>
  static YesType SubclassCheck(U<W...>*);
  static NoType SubclassCheck(...);
  static T* t_;

public:
  static const bool value = sizeof(SubclassCheck(t_)) == sizeof(YesType);
};

template <typename T, template <typename V, size_t W> class U>
struct IsSubclassOfTemplateTypenameSize {
private:
  typedef char YesType;
  struct NoType {
    char padding[8];
  };

  template <typename X, size_t Y>
  static YesType SubclassCheck(U<X, Y>*);
  static NoType SubclassCheck(...);
  static T* t_;

public:
  static const bool value = sizeof(SubclassCheck(t_)) == sizeof(YesType);
};

template <typename T, template <typename V, size_t W, typename X> class U>
struct IsSubclassOfTemplateTypenameSizeTypename {
private:
  typedef char YesType;
  struct NoType {
    char padding[8];
  };

  template <typename Y, size_t Z, typename A>
  static YesType SubclassCheck(U<Y, Z, A>*);
  static NoType SubclassCheck(...);
  static T* t_;

public:
  static const bool value = sizeof(SubclassCheck(t_)) == sizeof(YesType);
};

namespace internal {

template <typename T>
concept HasStackAllocatedMarker =
    requires { typename T::IsStackAllocatedTypeMarker; };

} // namespace internal

template <typename T>
class IsStackAllocatedType {
public:
  static constexpr bool value = internal::HasStackAllocatedMarker<T>;
};

template <typename T, typename U>
class IsStackAllocatedType<std::pair<T, U>> {
public:
  static constexpr bool value =
      IsStackAllocatedType<T>::value || IsStackAllocatedType<U>::value;
};

template <typename T>
class IsStackAllocatedType<std::optional<T>> {
public:
  static constexpr bool value = IsStackAllocatedType<T>::value;
};

template <typename... Ts>
class IsStackAllocatedType<std::variant<Ts...>> {
public:
  static constexpr bool value = std::disjunction_v<IsStackAllocatedType<Ts>...>;
};

template <typename T>
concept IsStackAllocatedTypeV = IsStackAllocatedType<T>::value;

} // namespace bkit
