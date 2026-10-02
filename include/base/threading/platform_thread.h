#pragma once
#include <windows.h>
namespace base {
using PlatformThreadId = DWORD;
class PlatformThread {
public:
  static PlatformThreadId CurrentId() {
    return ::GetCurrentThreadId();
  }
};
} // namespace base
