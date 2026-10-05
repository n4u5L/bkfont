# Ported from: chromium/third_party/ots/BUILD.gn
# Ported from: chromium/third_party/woff2/BUILD.gn
# Local CMake adaptation of the OTS and woff2_dec source lists.
include_guard(GLOBAL)

if(NOT DEFINED BLINK_FONTS_CHROMIUM_ROOT)
  message(FATAL_ERROR "BLINK_FONTS_CHROMIUM_ROOT must name the Chromium source root")
endif()
if(WIN32 AND NOT DEFINED BLINK_FONTS_PACKAGES_ROOT)
  message(FATAL_ERROR "BLINK_FONTS_PACKAGES_ROOT must name the prebuilt package directory")
endif()

if(NOT TARGET ZLIB::ZLIB)
  find_package(ZLIB CONFIG REQUIRED
    PATHS "${BLINK_FONTS_PACKAGES_ROOT}/zlib_1.3.2-x64/lib/cmake/zlib"
    NO_DEFAULT_PATH)
endif()
if(NOT TARGET Brotli::brotlidec)
  find_package(Brotli CONFIG REQUIRED
    PATHS "${BLINK_FONTS_PACKAGES_ROOT}/brotli_1.2.0-x64/lib/cmake/Brotli"
    NO_DEFAULT_PATH)
endif()

# The installed packages contain separate Debug and Release static libraries.
if(WIN32)
  set_target_properties(ZLIB::ZLIB PROPERTIES
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE
    MAP_IMPORTED_CONFIG_MINSIZEREL RELEASE)
endif()

set(_bkfont_woff2_root
  "${BLINK_FONTS_CHROMIUM_ROOT}/third_party/woff2")
add_library(bkfont_woff2_dec STATIC
  "${_bkfont_woff2_root}/src/table_tags.cc"
  "${_bkfont_woff2_root}/src/variable_length.cc"
  "${_bkfont_woff2_root}/src/woff2_common.cc"
  "${_bkfont_woff2_root}/src/woff2_dec.cc"
  "${_bkfont_woff2_root}/src/woff2_out.cc")
add_library(bkfont::woff2_dec ALIAS bkfont_woff2_dec)
target_include_directories(bkfont_woff2_dec PUBLIC
  "${_bkfont_woff2_root}/include")
target_link_libraries(bkfont_woff2_dec PUBLIC Brotli::brotlidec)
if(MSVC)
  # Matches the Windows warning option in Chromium's woff2_dec target.
  target_compile_options(bkfont_woff2_dec PRIVATE /wd4267)
endif()

set(_bkfont_ots_root
  "${BLINK_FONTS_CHROMIUM_ROOT}/third_party/ots/src")
add_library(bkfont_ots STATIC
  "${_bkfont_ots_root}/src/avar.cc"
  "${_bkfont_ots_root}/src/cff.cc"
  "${_bkfont_ots_root}/src/cff_charstring.cc"
  "${_bkfont_ots_root}/src/cmap.cc"
  "${_bkfont_ots_root}/src/cvar.cc"
  "${_bkfont_ots_root}/src/cvt.cc"
  "${_bkfont_ots_root}/src/fpgm.cc"
  "${_bkfont_ots_root}/src/fvar.cc"
  "${_bkfont_ots_root}/src/gasp.cc"
  "${_bkfont_ots_root}/src/gdef.cc"
  "${_bkfont_ots_root}/src/glyf.cc"
  "${_bkfont_ots_root}/src/gpos.cc"
  "${_bkfont_ots_root}/src/gsub.cc"
  "${_bkfont_ots_root}/src/gvar.cc"
  "${_bkfont_ots_root}/src/hdmx.cc"
  "${_bkfont_ots_root}/src/head.cc"
  "${_bkfont_ots_root}/src/hhea.cc"
  "${_bkfont_ots_root}/src/hvar.cc"
  "${_bkfont_ots_root}/src/kern.cc"
  "${_bkfont_ots_root}/src/layout.cc"
  "${_bkfont_ots_root}/src/loca.cc"
  "${_bkfont_ots_root}/src/ltsh.cc"
  "${_bkfont_ots_root}/src/math.cc"
  "${_bkfont_ots_root}/src/maxp.cc"
  "${_bkfont_ots_root}/src/metrics.cc"
  "${_bkfont_ots_root}/src/mvar.cc"
  "${_bkfont_ots_root}/src/name.cc"
  "${_bkfont_ots_root}/src/os2.cc"
  "${_bkfont_ots_root}/src/ots.cc"
  "${_bkfont_ots_root}/src/post.cc"
  "${_bkfont_ots_root}/src/prep.cc"
  "${_bkfont_ots_root}/src/stat.cc"
  "${_bkfont_ots_root}/src/variations.cc"
  "${_bkfont_ots_root}/src/vdmx.cc"
  "${_bkfont_ots_root}/src/vhea.cc"
  "${_bkfont_ots_root}/src/vorg.cc"
  "${_bkfont_ots_root}/src/vvar.cc")
add_library(bkfont::ots ALIAS bkfont_ots)
target_include_directories(bkfont_ots PUBLIC
  "${_bkfont_ots_root}/include")
target_link_libraries(bkfont_ots PUBLIC
  bkfont::woff2_dec
  Brotli::brotlidec
  ZLIB::ZLIB)

unset(_bkfont_woff2_root)
unset(_bkfont_ots_root)
