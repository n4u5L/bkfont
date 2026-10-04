#pragma once
#include <windows.h>
namespace bkfont::base {
using PlatformThreadId = DWORD;
class PlatformThread {
public:
  static PlatformThreadId CurrentId() {
    return ::GetCurrentThreadId();
  }
};
} // namespace bkfont::base
