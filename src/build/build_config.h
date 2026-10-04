// This extraction targets Windows. No other platform backend is selected.
#pragma once
#define BUILDFLAG(flag) (flag)
#define IS_WIN 1
#define IS_MAC 0
#define IS_IOS 0
#define IS_APPLE 0
#define IS_ANDROID 0
#define IS_LINUX 0
#define IS_CHROMEOS 0
#define IS_FUCHSIA 0
#define IS_POSIX 0
#define IS_OPENBSD 0
#define IS_FREEBSD 0
#define IS_ASMJS 0
#define COMPILER_MSVC 1
#define ARCH_CPU_LITTLE_ENDIAN 1
#if defined(_M_X64)
#define ARCH_CPU_X86_64 1
#define ARCH_CPU_X86_FAMILY 1
#define ARCH_CPU_64_BITS 1
#elif defined(_M_IX86)
#define ARCH_CPU_X86 1
#define ARCH_CPU_X86_FAMILY 1
#define ARCH_CPU_32_BITS 1
#elif defined(_M_ARM64)
#define ARCH_CPU_ARM64 1
#define ARCH_CPU_ARM_FAMILY 1
#define ARCH_CPU_64_BITS 1
#endif
