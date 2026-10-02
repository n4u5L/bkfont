// Compiler annotations required by the extracted Chromium source.
#pragma once
#define ALWAYS_INLINE inline
#define NOINLINE __declspec(noinline)
#define NO_UNIQUE_ADDRESS [[no_unique_address]]
#define NO_SANITIZE(...)
#define NO_SANITIZE_UNRELATED_CAST
#define NO_SANITIZE_CFI_ICALL
#define NO_SANITIZE_MEMORY
#define LIFETIME_BOUND
#define TRIVIAL_ABI
#define CONSTINIT constinit
#define PRETTY_FUNCTION __FUNCSIG__
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
#define CDECL __cdecl
#endif
#define STACK_UNINITIALIZED
#define GSL_POINTER
#define ENABLE_IF_ATTR(condition, message)
