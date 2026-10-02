// Source: base/containers/span.h
// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <array>
#include <compare>
#include <concepts>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>

#include "base/compiler_specific.h"
#include "base/containers/span_forward_internal.h"
#include "base/numerics/integral_constant_like.h"
#include "base/types/to_address.h"

// Non-owning views from Chromium. Runtime checks and checked iterators are
// removed; callers must satisfy the original bounds/lifetime preconditions.
// Size parameters retain this port's size_t interface.

namespace base {

// Provides a compile-time fixed extent to the `count` argument of the span
// constructor.
//
// (Not in `std::`.)
template <size_t N>
using fixed_extent = std::integral_constant<size_t, N>;

} // namespace base

// Mark `span` as satisfying the `view` and `borrowed_range` concepts. This
// should be done before the definition of `span`, so that any inlined calls to
// range functionality use the correct specializations.
template <typename ElementType, size_t Extent, typename InternalPtrType>
inline constexpr bool
    std::ranges::enable_view<base::span<ElementType, Extent, InternalPtrType>> =
        true;
template <typename ElementType, size_t Extent, typename InternalPtrType>
inline constexpr bool std::ranges::enable_borrowed_range<
    base::span<ElementType, Extent, InternalPtrType>> = true;

namespace base {

// Allows global use of a type for conversion to byte spans.
template <typename T>
inline constexpr bool kCanSafelyConvertToByteSpan =
    std::has_unique_object_representations_v<T>;
template <typename T, typename U>
inline constexpr bool kCanSafelyConvertToByteSpan<std::pair<T, U>> =
    kCanSafelyConvertToByteSpan<std::remove_cvref_t<T>> && kCanSafelyConvertToByteSpan<std::remove_cvref_t<U>>;

// Type tag to provide to byte span conversion functions to bypass
// `std::has_unique_object_representations_v<>` check.
struct allow_nonunique_obj_t {
  allow_nonunique_obj_t() = default;
};
inline constexpr allow_nonunique_obj_t allow_nonunique_obj{};

namespace internal {

// Exposition-only concept from [span.syn]
template <typename T>
inline constexpr size_t MaybeStaticExt = dynamic_extent;
template <typename T>
  requires IntegralConstantLike<T>
inline constexpr size_t MaybeStaticExt<T> = {T::value};

template <typename From, typename To>
concept LegalDataConversion = std::is_convertible_v<From (*)[], To (*)[]>;

// Akin to `std::constructible_from<span, T>`, but meant to be used in a
// type-deducing context where we don't know what args would be deduced;
// `std::constructible_from` can't be directly used in such a case since the
// type parameters must be fully-specified (e.g. `span<int>`), requiring us to
// have that knowledge already.
template <typename T>
concept SpanConstructibleFrom = requires(T&& t) { span(std::forward<T>(t)); };

// Returns the element type of `span(T)`.
template <typename T>
  requires SpanConstructibleFrom<T>
using ElementTypeOfSpanConstructedFrom =
    typename decltype(span(std::declval<T>()))::element_type;

template <typename T, typename It>
concept CompatibleIter =
    std::contiguous_iterator<It> && LegalDataConversion<std::remove_reference_t<std::iter_reference_t<It>>, T>;

// True when `T` is a `span`.
template <typename T>
inline constexpr bool kIsSpan = false;
template <typename ElementType, size_t Extent, typename InternalPtrType>
inline constexpr bool kIsSpan<span<ElementType, Extent, InternalPtrType>> =
    true;

template <typename T, typename R>
concept CompatibleRange =
    std::ranges::contiguous_range<R> && std::ranges::sized_range<R> && (std::ranges::borrowed_range<R> || (std::is_const_v<T>)) &&
    // `span`s should go through the copy constructor.
    (!kIsSpan<std::remove_cvref_t<R>> &&
     // Arrays should go through the array constructors.
     (!std::is_array_v<std::remove_cvref_t<R>>))
    && LegalDataConversion<
        std::remove_reference_t<std::ranges::range_reference_t<R>>,
        T>;

// Whether source object extent `X` will work to create a span of fixed extent
// `N`. This is not intended for use in dynamic-extent spans.
template <size_t N, size_t X>
concept FixedExtentConstructibleFromExtent = X == N || X == dynamic_extent;

// Computes a fixed extent if possible from a source container type `T`.
template <typename T>
inline constexpr size_t kComputedExtentImpl = dynamic_extent;
template <typename T>
  requires requires { std::tuple_size<T>(); }
inline constexpr size_t kComputedExtentImpl<T> = std::tuple_size_v<T>;
template <typename T, size_t N>
inline constexpr size_t kComputedExtentImpl<T[N]> = N;
template <typename T, size_t N>
inline constexpr size_t kComputedExtentImpl<std::span<T, N>> = N;
template <typename T, size_t N, typename InternalPtrType>
inline constexpr size_t kComputedExtentImpl<span<T, N, InternalPtrType>> = N;
template <typename T>
inline constexpr size_t kComputedExtent =
    kComputedExtentImpl<std::remove_cvref_t<T>>;

template <typename T>
concept CanSafelyConvertToByteSpan =
    kCanSafelyConvertToByteSpan<std::remove_cvref_t<T>>;

template <typename T>
concept ByteSpanConstructibleFrom =
    SpanConstructibleFrom<T> && CanSafelyConvertToByteSpan<ElementTypeOfSpanConstructedFrom<T>>;

#if !defined(_MSC_VER)
// Allows one-off use of a type that wouldn't normally convert to a byte span.
template <typename T>
concept CanSafelyConvertNonUniqueToByteSpan =
    // Non-trivially-copyable elements usually aren't safe even to serialize;
    // when they are that's normally unconditionally true and can be handled
    // using `kCanSafelyConvertToByteSpan`.
    std::is_trivially_copyable_v<T> &&
    // If this fails, `allow_nonunique_obj` wasn't necessary.
    !std::has_unique_object_representations_v<T>;
#else
template <typename T>
struct ByteSpanSafetyCheckSkippedForType {
  static constexpr bool value = false;
};

template <typename T>
concept CanSafelyConvertNonUniqueToByteSpan =
    ByteSpanSafetyCheckSkippedForType<T>::value == true || (std::is_trivially_copyable_v<T> && !std::has_unique_object_representations_v<T>);

// Used to bypass the safety check when compiling a class with std::atomic,
// which is not trivially copyable on MSVC
#define SKIP_BYTE_SPAN_SAFETY_CHECK_FOR(X)      \
  namespace base::internal {                    \
  template <>                                   \
  struct ByteSpanSafetyCheckSkippedForType<X> { \
    static constexpr bool value = true;         \
  };                                            \
  } // namespace base::internal
#endif // !defined(_MSC_VER)

template <typename T>
concept ByteSpanConstructibleFromNonUnique =
    SpanConstructibleFrom<T> && CanSafelyConvertNonUniqueToByteSpan<ElementTypeOfSpanConstructedFrom<T>>;

template <typename ByteType,
          typename ElementType,
          size_t Extent,
          typename InternalPtrType>
  requires((std::same_as<std::remove_const_t<ByteType>, char> || std::same_as<std::remove_const_t<ByteType>, unsigned char>) && (std::is_const_v<ByteType> || !std::is_const_v<ElementType>))
constexpr auto as_byte_span(
    span<ElementType, Extent, InternalPtrType> s) noexcept {
  constexpr size_t kByteExtent =
      Extent == dynamic_extent ? dynamic_extent : sizeof(ElementType) * Extent;
  // SAFETY: `s.data()` points to at least `s.size_bytes()` bytes' worth of
  // valid elements, so the size computed below must only contain valid
  // elements. Since `ByteType` is an alias to a character type, it has a size
  // of 1 byte, the resulting pointer has no alignment concerns, and it is not
  // UB to access memory contents inside the allocation through it.
  return UNSAFE_BUFFERS(span<ByteType, kByteExtent>(
      reinterpret_cast<ByteType*>(s.data()),
      s.size_bytes()));
}

} // namespace internal

// [span]: class `span` (non-dynamic `Extent`s)
template <typename ElementType, size_t Extent, typename InternalPtrType>
class GSL_POINTER span {
public:
  using element_type = ElementType;
  using value_type = std::remove_cv_t<element_type>;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  using pointer = element_type*;
  using const_pointer = const element_type*;
  using reference = element_type&;
  using const_reference = const element_type&;
  using iterator = element_type*;
  using const_iterator = const element_type*;
  using reverse_iterator = std::reverse_iterator<iterator>;
  // TODO(C++23): When `std::const_iterator<>` is available, switch to
  // `std::const_iterator<reverse_iterator>` as the standard specifies.
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  static constexpr size_type extent = Extent;

  // [span.cons]: Constructors, copy, and assignment
  // Default constructor.
  constexpr span() noexcept
    requires(extent == 0)
  = default;

  // Iterator + count.
  template <typename It>
    requires(internal::CompatibleIter<element_type, It>)
  // PRECONDITIONS: `first` must point to the first of at least `count`
  // contiguous valid elements.
  UNSAFE_BUFFER_USAGE constexpr span(It first, size_type count)
      : data_(to_address(first)) {

    // Non-zero `count` implies non-null `data_`. Use `SpanOrSize<T>` to
    // represent a size that might not be accompanied by the actual data.
  }

  // Iterator + sentinel.
  template <typename It, typename End>
    requires(internal::CompatibleIter<element_type, It> && std::sized_sentinel_for<End, It> && !std::is_convertible_v<End, size_t>)
  // PRECONDITIONS: `first` and `last` must be for the same allocation and all
  // elements in the range [first, last) must be valid.
  UNSAFE_BUFFER_USAGE constexpr span(It first, End last)
      // SAFETY: The caller must guarantee that `first` and `last` point into
      // the same allocation. In this case, the extent will be the number of
      // elements between the iterators and thus a valid size for the pointer to
      // the element at `first`.
      : UNSAFE_BUFFERS(span(first, static_cast<size_type>(last - first))) {
  }

  // Array of size `extent`.
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr span(
      std::type_identity_t<element_type> (&arr LIFETIME_BOUND)[extent]) noexcept
      // SAFETY: The type signature guarantees `arr` contains `extent` elements.
      : UNSAFE_BUFFERS(span(arr, extent)) {
  }

  // Range.
  template <typename R, size_t N = internal::kComputedExtent<R>>
    requires(internal::CompatibleRange<element_type, R> && internal::FixedExtentConstructibleFromExtent<extent, N> && !std::ranges::borrowed_range<R>)
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr explicit(N != extent) span(R&& range LIFETIME_BOUND)
      // SAFETY: `std::ranges::size()` returns the number of elements
      // `std::ranges::data()` will point to, so accessing those elements will
      // be safe.
      : UNSAFE_BUFFERS(
            span(std::ranges::data(range), std::ranges::size(range))) {
  }
  template <typename R, size_t N = internal::kComputedExtent<R>>
    requires(internal::CompatibleRange<element_type, R> && internal::FixedExtentConstructibleFromExtent<extent, N> && std::ranges::borrowed_range<R>)
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr explicit(N != extent) span(R&& range)
      // SAFETY: `std::ranges::size()` returns the number of elements
      // `std::ranges::data()` will point to, so accessing those elements will
      // be safe.
      : UNSAFE_BUFFERS(
            span(std::ranges::data(range), std::ranges::size(range))) {
  }

  // Initializer list.
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr explicit span(std::initializer_list<value_type> il LIFETIME_BOUND)
    requires(std::is_const_v<element_type>)
      // SAFETY: `size()` is exactly the number of elements in the initializer
      // list, so accessing that many will be safe.
      : UNSAFE_BUFFERS(span(il.begin(), il.size())) {
  }

  // Copy and move.
  constexpr span(const span& other) noexcept = default;
  template <typename OtherElementType,
            size_t OtherExtent,
            typename OtherInternalPtrType>
    requires((OtherExtent == dynamic_extent || extent == OtherExtent) && internal::LegalDataConversion<OtherElementType, element_type>)
  constexpr explicit(OtherExtent == dynamic_extent)
      span(const span<OtherElementType, OtherExtent, OtherInternalPtrType>&
               other) noexcept
      // SAFETY: `size()` is the number of elements that can be safely accessed
      // at `data()`.
      : UNSAFE_BUFFERS(span(other.data(), other.size())) {
  }
  constexpr span(span&& other) noexcept = default;

  // Copy and move assignment.
  constexpr span& operator=(const span& other) noexcept = default;
  constexpr span& operator=(span&& other) noexcept = default;

  // Performs a deep copy of the elements referenced by `other` to those
  // referenced by `this`. The spans must be the same size.
  //
  // If it's known the spans can not overlap, `copy_from_nonoverlapping()`
  // provides an unsafe alternative that avoids intermediate copies.
  //
  // (Not in `std::`; inspired by Rust's `slice::copy_from_slice()`.)
  constexpr void copy_from(span<const element_type, extent> other)
    requires(!std::is_const_v<element_type>)
  {
    if (std::is_constant_evaluated()) {
      // Comparing pointers to different objects at compile time yields
      // unspecified behavior, which would halt compilation. Instead,
      // unconditionally use a separate buffer in the constexpr context. This
      // would be inefficient at runtime, but that's irrelevant.

      // operator[] does not exist if extent == 0.
      if constexpr (extent > 0) {
        // Hold each value to be copied in a union so `element_type` does not
        // need to be default constructible.
        union Holder {
          constexpr Holder() {
          }
          constexpr ~Holder() {
          }
          element_type value;
        };
        // std::unique_ptr<T[]> isn't constexpr enough prior to C++23; another
        // alternative is std::vector, but that requires including <vector> just
        // for this edge case.
        Holder* buffer = new Holder[extent];
        for (size_t i = 0; i < extent; ++i) {
          // SAFETY: `buffers` is allocated with `extent` elements, and the loop
          // body only executes if `i < extent`.
          std::construct_at(&UNSAFE_BUFFERS(buffer[i]).value, other[i]);
        }
        for (size_t i = 0; i < extent; ++i) {
          // SAFETY: `buffers` is allocated with `extent` elements, and the loop
          // body only executes if `i < extent`.
          (*this)[i] = UNSAFE_BUFFERS(buffer[i]).value;
          UNSAFE_BUFFERS(buffer[i]).value.~element_type();
        }
        delete[] buffer;
      }
    } else {
      // Using `<=` to compare pointers to different allocations is UB;
      // reinterpret_cast is the workaround.
      if (reinterpret_cast<uintptr_t>(to_address(begin())) <= reinterpret_cast<uintptr_t>(to_address(other.begin()))) {
        std::ranges::copy(other, begin());
      } else {
        std::ranges::copy_backward(other, end());
      }
    }
  }
  template <typename R, size_t N = internal::kComputedExtent<R>>
    requires(!std::is_const_v<element_type> &&
             // Fixed-extent ranges should implicitly convert to use the
             // overload above; if they don't, it's because the extent doesn't
             // match. Rejecting this here improves the resulting errors.
             N == dynamic_extent && std::convertible_to<R &&, span<const element_type>>)
  constexpr void copy_from(R&& other) {
    // The dynamic-extent source must have the same number of elements.
    copy_from(span<const element_type, extent>(std::forward<R>(other)));
  }

  // Like `copy_from()`, but may be more performant; however, the caller must
  // guarantee the spans do not overlap, or this will invoke UB.
  //
  // (Not in `std::`; inspired by Rust's `slice::copy_from_slice()`.)
  constexpr void copy_from_nonoverlapping(
      span<const element_type, extent> other)
    requires(!std::is_const_v<element_type>)
  {
    // Comparing pointers to different objects at compile time yields
    // unspecified behavior, which would halt compilation. Instead implement in
    // terms of the guaranteed-safe behavior; performance is irrelevant in the
    // constexpr context.
    if (std::is_constant_evaluated()) {
      copy_from(other);
      return;
    }

    // See comments in `copy_from()` re: use of templated comparison objects.
    std::ranges::copy(other, begin());
  }
  template <typename R, size_t N = internal::kComputedExtent<R>>
    requires(!std::is_const_v<element_type> && N == dynamic_extent && std::convertible_to<R &&, span<const element_type>>)
  constexpr void copy_from_nonoverlapping(R&& other) {
    // The dynamic-extent source must have the same number of elements.
    copy_from_nonoverlapping(
        span<const element_type, extent>(std::forward<R>(other)));
  }

  // Like `copy_from()`, but allows the source to be smaller than this span, and
  // will only copy as far as the source size, leaving the remaining elements of
  // this span unwritten.
  //
  // (Not in `std::`; allows caller code to elide repeated size information and
  // makes it easier to preserve fixed-extent spans in the process.)
  template <typename R, size_t N = internal::kComputedExtent<R>>
    requires(!std::is_const_v<element_type> && (N <= extent || N == dynamic_extent) && std::convertible_to<R &&, span<const element_type>>)
  constexpr void copy_prefix_from(R&& other) {
    if constexpr (N == dynamic_extent) {
      return first(other.size()).copy_from(other);
    } else {
      return first<N>().copy_from(other);
    }
  }

  // Implicit conversion to fixed-extent `std::span<>`. (The fixed-extent
  // `std::span` range constructor is explicit.)
  // NOLINTNEXTLINE(google-explicit-constructor)
  operator std::span<element_type, extent>() const {
    return std::span<element_type, extent>(*this);
  }
  // NOLINTNEXTLINE(google-explicit-constructor)
  operator std::span<const element_type, extent>() const
    requires(!std::is_const_v<element_type>)
  {
    return std::span<const element_type, extent>(*this);
  }

  // [span.sub]: Subviews
  // First `count` elements.
  template <size_t Count>
  constexpr auto first() const
    requires(Count <= extent)
  {
    // SAFETY: `data()` points to at least `extent` elements, so the new data
    // scope is a strict subset of the old.
    return UNSAFE_BUFFERS(span<element_type, Count>(data(), Count));
  }
  constexpr auto first(size_type count) const {
    // SAFETY: `data()` points to at least `extent` elements, so the new data
    // scope is a strict subset of the old.
    return UNSAFE_BUFFERS(span<element_type>(data(), count));
  }

  // Last `count` elements.
  template <size_t Count>
  constexpr auto last() const
    requires(Count <= extent)
  {
    // SAFETY: `data()` points to at least `extent` elements, so the new data
    // scope is a strict subset of the old.
    return UNSAFE_BUFFERS(
        span<element_type, Count>(data() + (extent - Count), Count));
  }
  constexpr auto last(size_type count) const {
    // SAFETY: `data()` points to at least `extent` elements, so the new data
    // scope is a strict subset of the old.
    return UNSAFE_BUFFERS(
        span<element_type>(data() + (extent - size_type{count}), count));
  }

  // `count` elements beginning at `offset`.
  template <size_t Offset, size_t Count = dynamic_extent>
  constexpr auto subspan() const
    requires(Offset <= extent && (Count == dynamic_extent || Count <= extent - Offset))
  {
    if constexpr (Count == dynamic_extent) {
      constexpr size_t kRemaining = extent - Offset;
      // SAFETY: `data()` points to at least `extent` elements, so `Offset`
      // specifies a valid element index or the past-the-end index, and
      // `kRemaining` cannot index past-the-end elements.
      return UNSAFE_BUFFERS(
          span<element_type, kRemaining>(data() + Offset, kRemaining));
    } else {
      // SAFETY: `data()` points to at least `extent` elements, so `Offset`
      // specifies a valid element index or the past-the-end index, and `Count`
      // is no larger than the number of remaining valid elements.
      return UNSAFE_BUFFERS(span<element_type, Count>(data() + Offset, Count));
    }
  }
  constexpr auto subspan(size_type offset) const {
    const size_type remaining = extent - size_type{offset};
    // SAFETY: `data()` points to at least `extent` elements, so `offset`
    // specifies a valid element index or the past-the-end index, and
    // `remaining` cannot index past-the-end elements.
    return UNSAFE_BUFFERS(
        span<element_type>(data() + size_type{offset}, remaining));
  }
  constexpr auto subspan(size_type offset,
                         size_type count) const {
    // base does not allow dynamic_extent in two-arg subspan().
    // SAFETY: `data()` points to at least `extent` elements, so `offset`
    // specifies a valid element index or the past-the-end index, and `count` is
    // no larger than the number of remaining valid elements.
    return UNSAFE_BUFFERS(
        span<element_type>(data() + size_type{offset}, count));
  }

  // Splits a span a given offset, returning a pair of spans that cover the
  // ranges strictly before the offset and starting at the offset, respectively.
  //
  // (Not in `std::span`; inspired by Rust's `slice::split_at()` and
  // `split_at_mut()`.)
  template <size_t Offset>
    requires(Offset <= extent)
  constexpr auto split_at() const {
    return std::pair(first<Offset>(), subspan<Offset, extent - Offset>());
  }
  constexpr auto split_at(size_type offset) const {
    return std::pair(first(offset), subspan(offset));
  }

  // [span.obs]: Observers
  // Size.
  constexpr size_type size() const noexcept {
    return extent;
  }
  constexpr size_type size_bytes() const noexcept {
    return extent * sizeof(element_type);
  }

  // Empty.
  [[nodiscard]] constexpr bool empty() const noexcept {
    return extent == 0;
  }

  // Returns true if `lhs` and `rhs` are equal-sized and are per-element equal.
  //
  // (Not in `std::span`; improves both ergonomics and safety.)
  //
  // NOTE: Using non-members here intentionally allows comparing types that
  // implicitly convert to `span`.
  friend constexpr bool operator==(span lhs, span rhs)
    requires(std::is_const_v<element_type> && std::equality_comparable<const element_type>)
  {
    return std::ranges::equal(span<const element_type, extent>(lhs),
                              span<const element_type, extent>(rhs));
  }
  friend constexpr bool operator==(span lhs,
                                   span<const element_type, extent> rhs)
    requires(!std::is_const_v<element_type> && std::equality_comparable<const element_type>)
  {
    return std::ranges::equal(span<const element_type, extent>(lhs), rhs);
  }
  template <typename OtherElementType,
            size_t OtherExtent,
            typename OtherInternalPtrType>
    requires((OtherExtent == dynamic_extent || extent == OtherExtent) && std::equality_comparable_with<const element_type, const OtherElementType>)
  friend constexpr bool operator==(
      span lhs,
      span<OtherElementType, OtherExtent, OtherInternalPtrType> rhs) {
    return std::ranges::equal(span<const element_type, extent>(lhs),
                              span<const OtherElementType, OtherExtent>(rhs));
  }

  // Performs lexicographical comparison of `lhs` and `rhs`.
  //
  // (Not in `std::span`; improves both ergonomics and safety.)
  //
  // NOTE: Using non-members here intentionally allows comparing types that
  // implicitly convert to `span`.
  friend constexpr auto operator<=>(span lhs, span rhs)
    requires(std::is_const_v<element_type> && std::three_way_comparable<const element_type>)
  {
    const auto const_lhs = span<const element_type>(lhs);
    const auto const_rhs = span<const element_type>(rhs);
    return std::lexicographical_compare_three_way(
        const_lhs.begin(),
        const_lhs.end(),
        const_rhs.begin(),
        const_rhs.end());
  }
  friend constexpr auto operator<=>(span lhs,
                                    span<const element_type, extent> rhs)
    requires(!std::is_const_v<element_type> && std::three_way_comparable<const element_type>)
  {
    return span<const element_type>(lhs) <=> rhs;
  }
  template <typename OtherElementType,
            size_t OtherExtent,
            typename OtherInternalPtrType>
    requires((OtherExtent == dynamic_extent || extent == OtherExtent) && std::three_way_comparable_with<const element_type, const OtherElementType>)
  friend constexpr auto operator<=>(
      span lhs,
      span<OtherElementType, OtherExtent, OtherInternalPtrType> rhs) {
    const auto const_lhs = span<const element_type>(lhs);
    const auto const_rhs = span<const OtherElementType, OtherExtent>(rhs);
    return std::lexicographical_compare_three_way(
        const_lhs.begin(),
        const_lhs.end(),
        const_rhs.begin(),
        const_rhs.end());
  }

  // [span.elem]: Element access
  // Reference to specific element.
  // The index must be less than size().
  constexpr reference operator[](size_type idx) const
    requires(extent > 0)
  {
    return at(idx);
  }
  // The index must be less than size().
  constexpr reference at(size_type idx) const
    requires(extent > 0)
  {
    return *get_at(idx);
  }

  // Returns a pointer to an element in the span.
  //
  // (Not in `std::`; necessary when underlying memory is not yet initialized.)
  constexpr pointer get_at(size_type idx) const
    requires(extent > 0)
  {
    // SAFETY: `data()` points to at least `extent` elements, so `idx` must be
    // the index of a valid element.
    return UNSAFE_BUFFERS(data() + size_type{idx});
  }

  // Reference to first/last elements.
  // The span must contain at least one element.
  constexpr reference front() const
    requires(extent > 0)
  {
    return operator[](0);
  }
  // The span must contain at least one element.
  constexpr reference back() const
    requires(extent > 0)
  {
    return operator[](size() - 1);
  }

  // Underlying memory.
  constexpr pointer data() const noexcept {
    return data_;
  }

  // [span.iter]: Iterator support
  // Forward iterators.
  constexpr iterator begin() const noexcept {
    return data();
  }
  constexpr const_iterator cbegin() const noexcept {
    return const_iterator(begin());
  }
  constexpr iterator end() const noexcept {
    return data() + extent;
  }
  constexpr const_iterator cend() const noexcept {
    return const_iterator(end());
  }

  // Reverse iterators.
  constexpr reverse_iterator rbegin() const noexcept {
    return reverse_iterator(end());
  }
  constexpr const_reverse_iterator crbegin() const noexcept {
    return const_reverse_iterator(rbegin());
  }
  constexpr reverse_iterator rend() const noexcept {
    return reverse_iterator(begin());
  }
  constexpr const_reverse_iterator crend() const noexcept {
    return const_reverse_iterator(rend());
  }

private:
  InternalPtrType data_ = nullptr;
};

// [span]: class <span> (dynamic `Extent`)
template <typename ElementType, typename InternalPtrType>
class GSL_POINTER span<ElementType, dynamic_extent, InternalPtrType> {
public:
  using element_type = ElementType;
  using value_type = std::remove_cv_t<element_type>;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  using pointer = element_type*;
  using const_pointer = const element_type*;
  using reference = element_type&;
  using const_reference = const element_type&;
  using iterator = element_type*;
  using const_iterator = const element_type*;
  using reverse_iterator = std::reverse_iterator<iterator>;
  // TODO(C++23): When `std::const_iterator<>` is available, switch to
  // `std::const_iterator<reverse_iterator>` as the standard specifies.
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  static constexpr size_type extent = dynamic_extent;

  // [span.cons]: Constructors, copy, and assignment
  // Default constructor.
  constexpr span() noexcept = default;

  // Iterator + count.
  template <typename It>
    requires(internal::CompatibleIter<element_type, It>)
  // PRECONDITIONS: `first` must point to the first of at least `count`
  // contiguous valid elements.
  UNSAFE_BUFFER_USAGE constexpr span(It first, size_type count)
      : data_(to_address(first)),
        size_(count) {
    // Non-zero `count` implies non-null `data_`. Use `SpanOrSize<T>` to
    // represent a size that might not be accompanied by the actual data.
  }

  // Iterator + sentinel.
  template <typename It, typename End>
    requires(internal::CompatibleIter<element_type, It> && std::sized_sentinel_for<End, It> && !std::is_convertible_v<End, size_t>)
  // PRECONDITIONS: `first` and `last` must be for the same allocation and all
  // elements in the range [first, last) must be valid.
  UNSAFE_BUFFER_USAGE constexpr span(It first, End last)
      // SAFETY: The caller must guarantee that `first` and `last` point into
      // the same allocation. In this case, `size_` will be the number of
      // elements between the iterators and thus a valid size for the pointer to
      // the element at `first`.
      : UNSAFE_BUFFERS(span(first, static_cast<size_type>(last - first))) {
  }

  // Array of size N.
  template <size_t N>
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr span(
      std::type_identity_t<element_type> (&arr LIFETIME_BOUND)[N]) noexcept
      // SAFETY: The type signature guarantees `arr` contains `N` elements.
      : UNSAFE_BUFFERS(span(arr, N)) {
  }

  // Range.
  template <typename R>
    requires(internal::CompatibleRange<element_type, R>)
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr span(R&& range LIFETIME_BOUND)
      // SAFETY: `std::ranges::size()` returns the number of elements
      // `std::ranges::data()` will point to, so accessing those elements will
      // be safe.
      : UNSAFE_BUFFERS(
            span(std::ranges::data(range), std::ranges::size(range))) {
  }
  template <typename R>
    requires(internal::CompatibleRange<element_type, R> && std::ranges::borrowed_range<R>)
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr span(R&& range)
      // SAFETY: `std::ranges::size()` returns the number of elements
      // `std::ranges::data()` will point to, so accessing those elements will
      // be safe.
      : UNSAFE_BUFFERS(
            span(std::ranges::data(range), std::ranges::size(range))) {
  }

  // Initializer list.
  constexpr span(std::initializer_list<value_type> il LIFETIME_BOUND)
    requires(std::is_const_v<element_type>)
      // SAFETY: `size()` is exactly the number of elements in the initializer
      // list, so accessing that many will be safe.
      : UNSAFE_BUFFERS(span(il.begin(), il.size())) {
  }

  // Copy and move.
  constexpr span(const span& other) noexcept = default;
  template <typename OtherElementType,
            size_t OtherExtent,
            typename OtherInternalPtrType>
    requires(internal::LegalDataConversion<OtherElementType, element_type>)
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr span(
      const span<OtherElementType, OtherExtent, OtherInternalPtrType>&
          other) noexcept
      : data_(other.data()),
        size_(other.size()) {
  }
  constexpr span(span&& other) noexcept = default;

  // Copy and move assignment.
  constexpr span& operator=(const span& other) noexcept = default;
  constexpr span& operator=(span&& other) noexcept = default;

  // Performs a deep copy of the elements referenced by `other` to those
  // referenced by `this`. The spans must be the same size.
  //
  // If it's known the spans can not overlap, `copy_from_nonoverlapping()`
  // provides an unsafe alternative that avoids intermediate copies.
  //
  // (Not in `std::`; inspired by Rust's `slice::copy_from_slice()`.)
  constexpr void copy_from(span<const element_type> other)
    requires(!std::is_const_v<element_type>)
  {
    if (std::is_constant_evaluated()) {
      // Comparing pointers to different objects at compile time yields
      // unspecified behavior, which would halt compilation. Instead,
      // unconditionally use a separate buffer in the constexpr context. This
      // would be inefficient at runtime, but that's irrelevant.

      // Hold each value to be copied in a union so `element_type` does not
      // need to be default constructible.
      union Holder {
        constexpr Holder() {
        }
        constexpr ~Holder() {
        }
        element_type value;
      };
      // std::unique_ptr<T[]> isn't constexpr enough prior to C++23; another
      // alternative is std::vector, but that requires including <vector> just
      // for this edge case.
      Holder* buffer = new Holder[other.size()];
      for (size_t i = 0; i < other.size(); ++i) {
        // SAFETY: `buffers` is allocated with `other.size()` elements, and the
        // loop body only executes if `i < other.size()`.
        std::construct_at(&UNSAFE_BUFFERS(buffer[i]).value, other[i]);
      }
      for (size_t i = 0; i < other.size(); ++i) {
        // SAFETY: `buffers` is allocated with `other.size()` elements, and the
        // loop body only executes if `i < other.size()`.
        (*this)[i] = UNSAFE_BUFFERS(buffer[i]).value;
        UNSAFE_BUFFERS(buffer[i]).value.~element_type();
      }
      delete[] buffer;
    } else {
      // Using `<=` to compare pointers to different allocations is UB;
      // reinterpret_cast is the workaround.
      if (reinterpret_cast<uintptr_t>(to_address(begin())) <= reinterpret_cast<uintptr_t>(to_address(other.begin()))) {
        std::ranges::copy(other, begin());
      } else {
        std::ranges::copy_backward(other, end());
      }
    }
  }

  // Like `copy_from()`, but may be more performant; however, the caller must
  // guarantee the spans do not overlap, or this will invoke UB.
  //
  // (Not in `std::`; inspired by Rust's `slice::copy_from_slice()`.)
  constexpr void copy_from_nonoverlapping(span<const element_type> other)
    requires(!std::is_const_v<element_type>)
  {
    // Comparing pointers to different objects at compile time yields
    // unspecified behavior, which would halt compilation. Instead implement in
    // terms of the guaranteed-safe behavior; performance is irrelevant in the
    // constexpr context.
    if (std::is_constant_evaluated()) {
      copy_from(other);
      return;
    }

    // See comments in `copy_from()` re: use of templated comparison objects.
    std::ranges::copy(other, begin());
  }

  // Like `copy_from()`, but allows the source to be smaller than this span, and
  // will only copy as far as the source size, leaving the remaining elements of
  // this span unwritten.
  //
  // (Not in `std::`; allows caller code to elide repeated size information and
  // makes it easier to preserve fixed-extent spans in the process.)
  constexpr void copy_prefix_from(span<const element_type> other)
    requires(!std::is_const_v<element_type>)
  {
    return first(other.size()).copy_from(other);
  }

  // [span.sub]: Subviews
  // First `count` elements.
  template <size_t Count>
  constexpr auto first() const {
    // SAFETY: `data()` points to at least `size()` elements, so the new data
    // scope is a strict subset of the old.
    return UNSAFE_BUFFERS(span<element_type, Count>(data(), Count));
  }
  constexpr auto first(size_t count) const {
    // SAFETY: `data()` points to at least `size()` elements, so the new data
    // scope is a strict subset of the old.
    return UNSAFE_BUFFERS(span<element_type>(data(), count));
  }

  // Last `count` elements.
  template <size_t Count>
  constexpr auto last() const {
    // SAFETY: `data()` points to at least `size()` elements, so the new data
    // scope is a strict subset of the old.
    return UNSAFE_BUFFERS(
        span<element_type, Count>(data() + (size() - Count), Count));
  }
  constexpr auto last(size_type count) const {
    // SAFETY: `data()` points to at least `size()` elements, so the new data
    // scope is a strict subset of the old.
    return UNSAFE_BUFFERS(
        span<element_type>(data() + (size() - size_type{count}), count));
  }

  // `count` elements beginning at `offset`.
  template <size_t Offset, size_t Count = dynamic_extent>
  constexpr auto subspan() const {
    const size_type remaining = size() - Offset;
    if constexpr (Count == dynamic_extent) {
      // SAFETY: `data()` points to at least `size()` elements, so `Offset`
      // specifies a valid element index or the past-the-end index, and
      // `remaining` cannot index past-the-end elements.
      return UNSAFE_BUFFERS(
          span<element_type, Count>(data() + Offset, remaining));
    }
    // SAFETY: `data()` points to at least `size()` elements, so `Offset`
    // specifies a valid element index or the past-the-end index, and `Count` is
    // no larger than the number of remaining valid elements.
    return UNSAFE_BUFFERS(span<element_type, Count>(data() + Offset, Count));
  }
  constexpr auto subspan(size_type offset) const {
    const size_type remaining = size() - size_type{offset};
    // SAFETY: `data()` points to at least `size()` elements, so `offset`
    // specifies a valid element index or the past-the-end index, and
    // `remaining` cannot index past-the-end elements.
    return UNSAFE_BUFFERS(
        span<element_type>(data() + size_type{offset}, remaining));
  }
  constexpr auto subspan(size_type offset,
                         size_type count) const {
    // base does not allow dynamic_extent in two-arg subspan().
    // SAFETY: `data()` points to at least `size()` elements, so `offset`
    // specifies a valid element index or the past-the-end index, and `count` is
    // no larger than the number of remaining valid elements.
    return UNSAFE_BUFFERS(
        span<element_type>(data() + size_type{offset}, count));
  }

  // Splits a span a given offset, returning a pair of spans that cover the
  // ranges strictly before the offset and starting at the offset, respectively.
  //
  // (Not in `std::span`; inspired by Rust's `slice::split_at()` and
  // `split_at_mut()`.)
  template <size_t Offset>
  constexpr auto split_at() const {
    return std::pair(first<Offset>(), subspan<Offset>());
  }
  constexpr auto split_at(size_type offset) const {
    return std::pair(first(offset), subspan(offset));
  }

  // Returns a span of the first N elements, removing them.
  // Offset must not exceed size().
  //
  // (Not in `std::span`; convenient for processing a stream of disparate
  // objects or looping over elements.)
  template <size_t Offset>
  constexpr auto take_first() {
    const auto [first, rest] = split_at<Offset>();
    *this = rest;
    return first;
  }
  // The offset must not exceed size().
  constexpr auto take_first(size_type offset) {
    const auto [first, rest] = split_at(offset);
    *this = rest;
    return first;
  }

  // Returns the first element, removing it.
  // The span must contain at least one element.
  //
  // (Not in `std::span`; convenient for processing a stream of disparate
  // objects or looping over elements.)
  constexpr auto take_first_elem() {
    return take_first<1>().front();
  }

  // [span.obs]: Observers
  // Size.
  constexpr size_type size() const noexcept {
    return size_;
  }
  constexpr size_type size_bytes() const noexcept {
    return size() * sizeof(element_type);
  }

  // Empty.
  [[nodiscard]] constexpr bool empty() const noexcept {
    return size() == 0;
  }

  // Returns true if `lhs` and `rhs` are equal-sized and are per-element equal.
  //
  // (Not in `std::span`; improves both ergonomics and safety.)
  //
  // NOTE: Using non-members here intentionally allows comparing types that
  // implicitly convert to `span`.
  friend constexpr bool operator==(span lhs, span rhs)
    requires(std::is_const_v<element_type> && std::equality_comparable<const element_type>)
  {
    return std::ranges::equal(span<const element_type>(lhs),
                              span<const element_type>(rhs));
  }
  friend constexpr bool operator==(span lhs,
                                   span<const element_type, extent> rhs)
    requires(!std::is_const_v<element_type> && std::equality_comparable<const element_type>)
  {
    return std::ranges::equal(span<const element_type>(lhs), rhs);
  }
  template <typename OtherElementType,
            size_t OtherExtent,
            typename OtherInternalPtrType>
    requires(std::equality_comparable_with<const element_type,
                                           const OtherElementType>)
  friend constexpr bool operator==(
      span lhs,
      span<OtherElementType, OtherExtent, OtherInternalPtrType> rhs) {
    return std::ranges::equal(span<const element_type>(lhs),
                              span<const OtherElementType, OtherExtent>(rhs));
  }

  // Performs lexicographical comparison of `lhs` and `rhs`.
  //
  // (Not in `std::span`; improves both ergonomics and safety.)
  //
  // NOTE: Using non-members here intentionally allows comparing types that
  // implicitly convert to `span`.
  friend constexpr auto operator<=>(span lhs, span rhs)
    requires(std::is_const_v<element_type> && std::three_way_comparable<const element_type>)
  {
    const auto const_lhs = span<const element_type>(lhs);
    const auto const_rhs = span<const element_type>(rhs);
    return std::lexicographical_compare_three_way(
        const_lhs.begin(),
        const_lhs.end(),
        const_rhs.begin(),
        const_rhs.end());
  }
  friend constexpr auto operator<=>(span lhs,
                                    span<const element_type, extent> rhs)
    requires(!std::is_const_v<element_type> && std::three_way_comparable<const element_type>)
  {
    return span<const element_type>(lhs) <=> rhs;
  }
  template <typename OtherElementType,
            size_t OtherExtent,
            typename OtherInternalPtrType>
    requires(std::three_way_comparable_with<const element_type,
                                            const OtherElementType>)
  friend constexpr auto operator<=>(
      span lhs,
      span<OtherElementType, OtherExtent, OtherInternalPtrType> rhs) {
    const auto const_lhs = span<const element_type>(lhs);
    const auto const_rhs = span<const OtherElementType, OtherExtent>(rhs);
    return std::lexicographical_compare_three_way(
        const_lhs.begin(),
        const_lhs.end(),
        const_rhs.begin(),
        const_rhs.end());
  }

  // [span.elem]: Element access
  // Reference to a specific element.
  // The index must be less than size().
  constexpr reference operator[](size_type idx) const {
    return at(idx);
  }

  // The index must be less than size().
  constexpr reference at(size_type idx) const {
    return *get_at(idx);
  }

  // Returns a pointer to an element in the span.
  //
  // (Not in `std::`; necessary when underlying memory is not yet initialized.)
  constexpr pointer get_at(size_type idx) const {
    // SAFETY: `data()` points to at least `size()` elements, so `idx` must be
    // the index of a valid element.
    return UNSAFE_BUFFERS(data() + size_type{idx});
  }

  // Reference to first/last elements.
  // The span must contain at least one element.
  constexpr reference front() const {
    return operator[](0);
  }
  // The span must contain at least one element.
  constexpr reference back() const {
    return operator[](size() - 1);
  }

  // Underlying memory.
  constexpr pointer data() const noexcept {
    return data_;
  }

  // [span.iter]: Iterator support
  // Forward iterators.
  constexpr iterator begin() const noexcept {
    return data();
  }
  constexpr const_iterator cbegin() const noexcept {
    return const_iterator(begin());
  }
  constexpr iterator end() const noexcept {
    return data() + size();
  }
  constexpr const_iterator cend() const noexcept {
    return const_iterator(end());
  }

  // Reverse iterators.
  constexpr reverse_iterator rbegin() const noexcept {
    return reverse_iterator(end());
  }
  constexpr const_reverse_iterator crbegin() const noexcept {
    return const_reverse_iterator(rbegin());
  }
  constexpr reverse_iterator rend() const noexcept {
    return reverse_iterator(begin());
  }
  constexpr const_reverse_iterator crend() const noexcept {
    return const_reverse_iterator(rend());
  }

  // [span.objectrep]: Views of object representation
  // Converts a dynamic-extent span to a fixed-extent span. Returns a
  // `span<element_type, Extent>` iff `size() == Extent`; otherwise, returns
  // `std::nullopt`.
  //
  // (Not in `std::`; provides a conditional conversion path.)
  template <size_t Extent>
  constexpr std::optional<span<element_type, Extent>> to_fixed_extent() const {
    return size() == Extent ? std::optional(span<element_type, Extent>(*this))
                            : std::nullopt;
  }

private:
  InternalPtrType data_ = nullptr;
  size_t size_ = 0;
};

// [span.deduct]: Deduction guides
template <typename It, typename EndOrSize>
  requires(std::contiguous_iterator<It>)
span(It, EndOrSize) -> span<std::remove_reference_t<std::iter_reference_t<It>>,
                            internal::MaybeStaticExt<EndOrSize>>;

template <typename T, size_t N>
span(T (&)[N]) -> span<T, N>;

template <typename R>
  requires(std::ranges::contiguous_range<R>)
span(R&&) -> span<std::remove_reference_t<std::ranges::range_reference_t<R>>,
                  internal::kComputedExtent<R>>;

// Compatibility helper retained by the font port.
template <typename T, size_t Extent, typename InternalPtrType>
constexpr span<const T, Extent> as_const(span<T, Extent, InternalPtrType> s) {
  return span<const T, Extent>(s);
}

// [span.objectrep]: Views of object representation
template <typename ElementType, size_t Extent, typename InternalPtrType>
  requires(internal::CanSafelyConvertToByteSpan<ElementType>)
constexpr auto as_bytes(span<ElementType, Extent, InternalPtrType> s) {
  return internal::as_byte_span<const uint8_t>(s);
}
template <typename ElementType, size_t Extent, typename InternalPtrType>
  requires(internal::CanSafelyConvertNonUniqueToByteSpan<ElementType>)
constexpr auto as_bytes(allow_nonunique_obj_t,
                        span<ElementType, Extent, InternalPtrType> s) {
  return internal::as_byte_span<const uint8_t>(s);
}
template <typename ElementType, size_t Extent, typename InternalPtrType>
  requires(internal::CanSafelyConvertToByteSpan<ElementType> && !std::is_const_v<ElementType>)
constexpr auto as_writable_bytes(span<ElementType, Extent, InternalPtrType> s) {
  return internal::as_byte_span<uint8_t>(s);
}
template <typename ElementType, size_t Extent, typename InternalPtrType>
  requires(internal::CanSafelyConvertNonUniqueToByteSpan<ElementType> && !std::is_const_v<ElementType>)
constexpr auto as_writable_bytes(allow_nonunique_obj_t,
                                 span<ElementType, Extent, InternalPtrType> s) {
  return internal::as_byte_span<uint8_t>(s);
}

// Like `as_[writable_]bytes()`, but uses `[const] char` rather than `[const]
// uint8_t`.
//
// (Not in `std::`; eases span adoption in Chromium, which uses `char` in many
// cases that rightfully should be `uint8_t`.)
template <typename ElementType, size_t Extent, typename InternalPtrType>
  requires(internal::CanSafelyConvertToByteSpan<ElementType>)
constexpr auto as_chars(span<ElementType, Extent, InternalPtrType> s) {
  return internal::as_byte_span<const char>(s);
}
template <typename ElementType, size_t Extent, typename InternalPtrType>
  requires(internal::CanSafelyConvertNonUniqueToByteSpan<ElementType>)
constexpr auto as_chars(allow_nonunique_obj_t,
                        span<ElementType, Extent, InternalPtrType> s) {
  return internal::as_byte_span<const char>(s);
}
template <typename ElementType, size_t Extent, typename InternalPtrType>
  requires(internal::CanSafelyConvertToByteSpan<ElementType> && !std::is_const_v<ElementType>)
constexpr auto as_writable_chars(span<ElementType, Extent, InternalPtrType> s) {
  return internal::as_byte_span<char>(s);
}
template <typename ElementType, size_t Extent, typename InternalPtrType>
  requires(internal::CanSafelyConvertNonUniqueToByteSpan<ElementType> && !std::is_const_v<ElementType>)
constexpr auto as_writable_chars(allow_nonunique_obj_t,
                                 span<ElementType, Extent, InternalPtrType> s) {
  return internal::as_byte_span<char>(s);
}

// Converts a `T&` to a `span<T, 1>`.
//
// (Not in `std::`; inspired by Rust's `slice::from_ref()`.)
template <typename T>
constexpr auto span_from_ref(const T& t LIFETIME_BOUND) {
  // SAFETY: It's safe to read the memory at `t`'s address as long as the
  // provided reference is valid.
  return UNSAFE_BUFFERS(span<const T, 1>(std::addressof(t), 1u));
}
template <typename T>
constexpr auto span_from_ref(T& t LIFETIME_BOUND) {
  // SAFETY: It's safe to read the memory at `t`'s address as long as the
  // provided reference is valid.
  return UNSAFE_BUFFERS(span<T, 1>(std::addressof(t), 1u));
}

// Converts a `T&` to a `span<[const] uint8_t, sizeof(T)>`.
//
// (Not in `std::`.)
template <typename T>
  requires(internal::CanSafelyConvertToByteSpan<T>)
constexpr auto byte_span_from_ref(const T& t LIFETIME_BOUND) {
  return as_bytes(span_from_ref(t));
}
template <typename T>
  requires(internal::CanSafelyConvertNonUniqueToByteSpan<T>)
constexpr auto byte_span_from_ref(allow_nonunique_obj_t,
                                  const T& t LIFETIME_BOUND) {
  return as_bytes(allow_nonunique_obj, span_from_ref(t));
}
template <typename T>
  requires(internal::CanSafelyConvertToByteSpan<T>)
constexpr auto byte_span_from_ref(T& t LIFETIME_BOUND) {
  return as_writable_bytes(span_from_ref(t));
}
template <typename T>
  requires(internal::CanSafelyConvertNonUniqueToByteSpan<T>)
constexpr auto byte_span_from_ref(allow_nonunique_obj_t, T& t LIFETIME_BOUND) {
  return as_writable_bytes(allow_nonunique_obj, span_from_ref(t));
}

// Converts a `const CharT[]` literal to a `span<const CharT>`, omitting the
// trailing '\0' (internal '\0's, if any, are preserved). For comparison:
//   `span("hi")`                  => `span<const char, 3>({'h', 'i', '\0'})`
//   `span(std::string_view("hi")) => `span<const char>({'h', 'i'})`
//   `span_from_cstring("hi")`     => `span<const char, 2>({'h', 'i'})`
//
// (Not in `std::`; useful when reading and writing character subsequences in
// larger files.)
template <typename CharT, size_t Extent>
constexpr auto span_from_cstring(const CharT (&str LIFETIME_BOUND)[Extent]) {
  return span(str).template first<Extent - 1>();
}

// Converts a `const CharT[]` literal to a `span<const CharT>`, preserving the
// trailing '\0'.
//
// (Not in `std::`; identical to constructor behavior, but more explicit.)
template <typename CharT, size_t Extent>
constexpr auto span_with_nul_from_cstring(
    const CharT (&str LIFETIME_BOUND)[Extent]) {
  return span(str);
}

// Like `span_from_cstring()`, but returns a byte span.
//
// (Not in `std::`.)
template <typename CharT, size_t Extent>
constexpr auto byte_span_from_cstring(const CharT (&str LIFETIME_BOUND)[Extent]) {
  return as_bytes(span(str).template first<Extent - 1>());
}

// Like `span_with_nul_from_cstring()`, but returns a byte span.
//
// (Not in `std::`.)
template <typename CharT, size_t Extent>
constexpr auto byte_span_with_nul_from_cstring(
    const CharT (&str LIFETIME_BOUND)[Extent]) {
  return as_bytes(span(str));
}

// Converts an object which can already explicitly convert to some kind of span
// directly into a byte span.
//
// (Not in `std::`.)
template <int&... ExplicitArgumentBarrier, typename T>
  requires(internal::ByteSpanConstructibleFrom<const T&>)
constexpr auto as_byte_span(const T& t LIFETIME_BOUND) {
  return as_bytes(span(t));
}
template <int&... ExplicitArgumentBarrier, typename T>
  requires(internal::ByteSpanConstructibleFromNonUnique<const T&>)
constexpr auto as_byte_span(allow_nonunique_obj_t, const T& t LIFETIME_BOUND) {
  return as_bytes(allow_nonunique_obj, span(t));
}
template <int&... ExplicitArgumentBarrier, typename T>
  requires(internal::ByteSpanConstructibleFrom<const T&> && std::ranges::borrowed_range<T>)
constexpr auto as_byte_span(const T& t) {
  return as_bytes(span(t));
}
template <int&... ExplicitArgumentBarrier, typename T>
  requires(internal::ByteSpanConstructibleFromNonUnique<const T&> && std::ranges::borrowed_range<T>)
constexpr auto as_byte_span(allow_nonunique_obj_t, const T& t) {
  return as_bytes(allow_nonunique_obj, span(t));
}
// Array arguments require dedicated specializations because if only the
// generalized functions are available, the compiler cannot deduce the template
// parameter.
template <int&... ExplicitArgumentBarrier, typename ElementType, size_t Extent>
  requires(internal::CanSafelyConvertToByteSpan<ElementType>)
constexpr auto as_byte_span(const ElementType (&arr LIFETIME_BOUND)[Extent]) {
  return as_bytes(span<const ElementType, Extent>(arr));
}
template <int&... ExplicitArgumentBarrier, typename ElementType, size_t Extent>
  requires(internal::CanSafelyConvertNonUniqueToByteSpan<ElementType>)
constexpr auto as_byte_span(allow_nonunique_obj_t,
                            const ElementType (&arr LIFETIME_BOUND)[Extent]) {
  return as_bytes(allow_nonunique_obj, span<const ElementType, Extent>(arr));
}
template <int&... ExplicitArgumentBarrier, typename T>
  requires(internal::ByteSpanConstructibleFrom<T &&> && !std::is_const_v<internal::ElementTypeOfSpanConstructedFrom<T>>)
// NOTE: `t` is not marked as lifetimebound because the "non-const
// `element_type`" requirement above will in turn require `T` to be a borrowed
// range.
constexpr auto as_writable_byte_span(T&& t) {
  return as_writable_bytes(span(t));
}
template <int&... ExplicitArgumentBarrier, typename T>
  requires(internal::ByteSpanConstructibleFromNonUnique<T &&> && !std::is_const_v<internal::ElementTypeOfSpanConstructedFrom<T>>)
constexpr auto as_writable_byte_span(allow_nonunique_obj_t, T&& t) {
  return as_writable_bytes(allow_nonunique_obj, span(t));
}
template <int&... ExplicitArgumentBarrier, typename ElementType, size_t Extent>
  requires(internal::CanSafelyConvertToByteSpan<ElementType> && !std::is_const_v<ElementType>)
constexpr auto as_writable_byte_span(
    ElementType (&arr LIFETIME_BOUND)[Extent]) {
  return as_writable_bytes(span<ElementType, Extent>(arr));
}
template <int&... ExplicitArgumentBarrier, typename ElementType, size_t Extent>
  requires(internal::CanSafelyConvertNonUniqueToByteSpan<ElementType> && !std::is_const_v<ElementType>)
constexpr auto as_writable_byte_span(
    allow_nonunique_obj_t,
    ElementType (&arr LIFETIME_BOUND)[Extent]) {
  return as_writable_bytes(allow_nonunique_obj, span<ElementType, Extent>(arr));
}

} // namespace base
