// Ported from: chromium/build/build_config.h
// Platform/compiler subset used by the standalone Windows and Linux backends.
#pragma once
#define BUILDFLAG(flag) (flag)
#if defined(_WIN32)
#define IS_WIN 1
#define IS_LINUX 0
#define IS_POSIX 0
#define WCHAR_T_IS_16_BIT
#elif defined(__linux__) && !defined(__ANDROID__)
#define IS_WIN 0
#define IS_LINUX 1
#define IS_POSIX 1
#define WCHAR_T_IS_32_BIT
#else
#error Unsupported font platform.
#endif
#define IS_MAC 0
#define IS_IOS 0
#define IS_APPLE 0
#define IS_ANDROID 0
#define IS_CHROMEOS 0
#define IS_FUCHSIA 0
#define IS_OPENBSD 0
#define IS_FREEBSD 0
#define IS_ASMJS 0
#if defined(_MSC_VER)
#define COMPILER_MSVC 1
#elif defined(__GNUC__)
#define COMPILER_GCC 1
#endif
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define ARCH_CPU_BIG_ENDIAN 1
#else
#define ARCH_CPU_LITTLE_ENDIAN 1
#endif
#if defined(_M_X64) || defined(__x86_64__)
#define ARCH_CPU_X86_64 1
#define ARCH_CPU_X86_FAMILY 1
#define ARCH_CPU_64_BITS 1
#elif defined(_M_IX86) || defined(__i386__)
#define ARCH_CPU_X86 1
#define ARCH_CPU_X86_FAMILY 1
#define ARCH_CPU_32_BITS 1
#elif defined(_M_ARM64) || defined(__aarch64__)
#define ARCH_CPU_ARM64 1
#define ARCH_CPU_ARM_FAMILY 1
#define ARCH_CPU_64_BITS 1
#else
#error Unsupported font CPU architecture.
#endif
