# GLFW 3.5.1, built from source. Prefer the local checkout when available;
# a fresh checkout of bkfont downloads the same revision via FetchContent.
include(FetchContent)
if(NOT DEFINED FETCHCONTENT_SOURCE_DIR_GLFW AND
   EXISTS "${PROJECT_SOURCE_DIR}/third_party/glfw/CMakeLists.txt")
  set(FETCHCONTENT_SOURCE_DIR_GLFW "${PROJECT_SOURCE_DIR}/third_party/glfw")
endif()
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Build GLFW examples" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "Build GLFW tests" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "Build GLFW documentation" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "Install GLFW" FORCE)
set(GLFW_LIBRARY_TYPE STATIC)
FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG d9d6f0f1f967807ffade6598ea9a631ebaf37a56)
FetchContent_MakeAvailable(glfw)

find_package(OpenGL REQUIRED)
add_executable(bkfont_glfw_example text_glfw.cc)
target_link_libraries(bkfont_glfw_example PRIVATE blink_fonts::blink_fonts glfw OpenGL::GL)
# Source Han Serif SC Regular 2.003 (SIL OFL 1.1), bundled from:
# https://github.com/adobe-fonts/source-han-serif/tree/2.003R/OTF/SimplifiedChinese
# Load the file directly so the example does not require a system installation
# or a particular working directory.
target_compile_definitions(bkfont_glfw_example PRIVATE
  BKFONT_EXAMPLE_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fonts/SourceHanSerifSC-Regular.otf")
