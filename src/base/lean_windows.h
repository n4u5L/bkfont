// Ported from: skia/src/base/SkLeanWindows.h

#pragma once

#if defined(_WIN32)
// https://devblogs.microsoft.com/oldnewthing/20091130-00/?p=15863
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#define WIN32_IS_MEAN_WAS_LOCALLY_DEFINED
#endif
#ifndef NOMINMAX
#define NOMINMAX
#define NOMINMAX_WAS_LOCALLY_DEFINED
#endif

#include <windows.h>

#ifdef WIN32_IS_MEAN_WAS_LOCALLY_DEFINED
#undef WIN32_IS_MEAN_WAS_LOCALLY_DEFINED
#undef WIN32_LEAN_AND_MEAN
#endif
#ifdef NOMINMAX_WAS_LOCALLY_DEFINED
#undef NOMINMAX_WAS_LOCALLY_DEFINED
#undef NOMINMAX
#endif
#endif // _WIN32
