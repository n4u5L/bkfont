# GLFW 3.5.1, built from source. Prefer the local checkout when available;
# a fresh checkout of bkit downloads the same revision via FetchContent.
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
add_executable(bkit_inline_text_render_example inline_text_render.cc)
target_link_libraries(bkit_inline_text_render_example PRIVATE bkit::bkit glfw OpenGL::GL)

# Noto Sans Egyptian Hieroglyphs (SIL Open Font License 1.1) for the Ancient
# Egyptian card, which loads it from file as @font-face would. Downloaded at
# configure time from a pinned google/fonts revision; without it the card
# falls back to the system fonts.
set(BKIT_EXAMPLE_FONT_DIR "${CMAKE_CURRENT_BINARY_DIR}/fonts")
set(BKIT_NOTO_EGYPTIAN_URL
  "https://raw.githubusercontent.com/google/fonts/8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5/ofl/notosansegyptianhieroglyphs")
function(bkit_download_example_font name sha256)
  set(path "${BKIT_EXAMPLE_FONT_DIR}/${name}")
  if(EXISTS "${path}")
    file(SHA256 "${path}" actual)
    if(actual STREQUAL sha256)
      return()
    endif()
  endif()
  file(DOWNLOAD "${BKIT_NOTO_EGYPTIAN_URL}/${name}" "${path}"
    EXPECTED_HASH SHA256=${sha256} TLS_VERIFY ON STATUS status)
  list(GET status 0 code)
  if(NOT code EQUAL 0)
    file(REMOVE "${path}")
    message(WARNING "Could not download ${name} (${status}); the Ancient Egyptian card uses system fonts.")
  endif()
endfunction()
bkit_download_example_font(NotoSansEgyptianHieroglyphs-Regular.ttf
  b792accc6207ca9caa59fc67f53926b6985a99d255dd432d7c68b68a6dfd62f8)
bkit_download_example_font(OFL.txt dc8114a49f5bb53bad3d99ee52cbb245e98076438d59428a258720258494de68)
target_compile_definitions(bkit_inline_text_render_example PRIVATE
  BKIT_EXAMPLE_FONT_DIR="${BKIT_EXAMPLE_FONT_DIR}")

# Editable Word-style document, using the same bkit layout/paint pipeline.
add_library(bkit_rich_text_model STATIC
  rich_text/piece_tree.cc
  rich_text/json.cc
  rich_text/style_catalog.cc
  rich_text/document.cc)
target_link_libraries(bkit_rich_text_model PUBLIC bkit::bkit)
add_executable(bkit_rich_text_example rich_text/editor.cc rich_text/document_view.cc)
target_link_libraries(bkit_rich_text_example PRIVATE bkit_rich_text_model glfw)
if(WIN32)
  target_sources(bkit_rich_text_example PRIVATE rich_text/window_presenter_win.cc)
  target_compile_definitions(bkit_rich_text_example PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
  target_link_libraries(bkit_rich_text_example PRIVATE comdlg32 imm32 gdi32 d3d11 dcomp dxgi ole32 dwmapi)
elseif(UNIX AND NOT APPLE)
  target_link_libraries(bkit_rich_text_example PRIVATE Fontconfig::Fontconfig OpenGL::GL)
endif()
