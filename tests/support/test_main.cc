// Standalone equivalent of Blink's platform test process initialization.
// TestingPlatformSupport::DefaultLocale() supplies en-US in the upstream suite.
#include <windows.h>
#include <objbase.h>
#include <cstdio>
#include <unicode/uclean.h>
#include <unicode/uloc.h>
#include "gtest/gtest.h"
#include "initialize.h"
#include "language.h"
#include "base/text/atomic_string.h"

int main(int argc, char** argv) {
  UErrorCode error = U_ZERO_ERROR;
  u_init(&error);
  // Chromium base/test/test_suite.cc fixes ICU's process locale to en_US.
  if (U_SUCCESS(error)) uloc_setDefault("en_US", &error);
  if (U_FAILURE(error)) {
    std::fprintf(stderr, "ICU initialization failed: %s\n", u_errorName(error));
    return 1;
  }
  const HRESULT com_result = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  blink::InitializeFonts();
  blink::OverrideUserPreferredLanguagesForTesting({blink::AtomicString("en-US")});
  // Parameter generators construct Blink objects during GoogleTest
  // registration, so WTF and the font environment must already be initialized.
  ::testing::InitGoogleTest(&argc, argv);
  const int result = RUN_ALL_TESTS();
  if (SUCCEEDED(com_result)) ::CoUninitialize();
  return result;
}
