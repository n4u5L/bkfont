// Ported from: chromium/base/compiler_specific.h

// Local implementation: compiler annotations for the standalone font port.
// Compiler annotations required by the extracted Chromium source.
#pragma once
#define ALWAYS_INLINE inline
#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#define PRETTY_FUNCTION __FUNCSIG__
#else
#define NOINLINE __attribute__((noinline))
#define PRETTY_FUNCTION __PRETTY_FUNCTION__
#endif
#define NO_UNIQUE_ADDRESS [[no_unique_address]]
#define NO_SANITIZE(...)
#define NO_SANITIZE_UNRELATED_CAST
#define NO_SANITIZE_CFI_ICALL
#define NO_SANITIZE_MEMORY
#define LIFETIME_BOUND
#define TRIVIAL_ABI
#define CONSTINIT constinit
#define UNSAFE_BUFFER_USAGE
#define UNSAFE_BUFFERS(...) __VA_ARGS__
#define UNSAFE_TODO(...) __VA_ARGS__
#define REINITIALIZES_AFTER_MOVE
#define ANALYZER_SKIP_THIS_PATH()
#define PRINTF_FORMAT(format_param, dots_param)
#define WARN_UNUSED_RESULT [[nodiscard]]
#define NOT_TAIL_CALLED
#define UNLIKELY(x) (x)
#define LIKELY(x) (x)
#ifndef CDECL
#if defined(_MSC_VER)
#define CDECL __cdecl
#else
#define CDECL
#endif
#endif
#define STACK_UNINITIALIZED
#define GSL_POINTER
#define ENABLE_IF_ATTR(condition, message)

// Upstream relies on an exhaustive switch and falls off the end of a non-void
// function, which MSVC warns about (C4715). Reaching it is undefined behavior,
// as falling off the end is upstream.
#if defined(_MSC_VER) && !defined(__clang__)
#define FALLS_OFF_END() __assume(0)
#else
#define FALLS_OFF_END() __builtin_unreachable()
#endif
