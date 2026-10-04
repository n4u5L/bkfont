# Build without C++ exceptions, as Chromium does. CMake's default /EHsc is
# removed so that it does not conflict with /EHs-c- (warning D9025).
include_guard(DIRECTORY)

if(MSVC)
  string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  add_compile_options(/EHs-c-)
  add_compile_definitions(_HAS_EXCEPTIONS=0)
else()
  add_compile_options(-fno-exceptions)
endif()
