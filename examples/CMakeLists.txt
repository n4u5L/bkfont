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
# Multilingual inline layout and the implemented CSS properties. Uses system
# fonts (DirectWrite/Fontconfig matching with fallback).
add_executable(bkfont_mushoku_tensei_example mushoku_tensei.cc)
target_link_libraries(bkfont_mushoku_tensei_example PRIVATE bkfont::bkfont glfw OpenGL::GL)

# Editable Word-style document, using the same bkfont layout/paint pipeline.
add_library(bkfont_rich_text_model STATIC
  rich_text/piece_tree.cc
  rich_text/json.cc
  rich_text/style_catalog.cc
  rich_text/document.cc)
target_link_libraries(bkfont_rich_text_model PUBLIC bkfont::bkfont)
add_executable(bkfont_rich_text_example rich_text/editor.cc rich_text/document_view.cc)
target_link_libraries(bkfont_rich_text_example PRIVATE bkfont_rich_text_model glfw OpenGL::GL)
if(WIN32)
  target_link_libraries(bkfont_rich_text_example PRIVATE comdlg32 imm32)
elseif(UNIX AND NOT APPLE)
  target_link_libraries(bkfont_rich_text_example PRIVATE Fontconfig::Fontconfig)
endif()
