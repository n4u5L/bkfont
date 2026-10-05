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

set(_blink_fonts_woff2_root
  "${BLINK_FONTS_CHROMIUM_ROOT}/third_party/woff2")
add_library(blink_fonts_woff2_dec STATIC
  "${_blink_fonts_woff2_root}/src/table_tags.cc"
  "${_blink_fonts_woff2_root}/src/variable_length.cc"
  "${_blink_fonts_woff2_root}/src/woff2_common.cc"
  "${_blink_fonts_woff2_root}/src/woff2_dec.cc"
  "${_blink_fonts_woff2_root}/src/woff2_out.cc")
target_include_directories(blink_fonts_woff2_dec PUBLIC
  "${_blink_fonts_woff2_root}/include")
target_link_libraries(blink_fonts_woff2_dec PUBLIC Brotli::brotlidec)
if(MSVC)
  # Matches the Windows warning option in Chromium's woff2_dec target.
  target_compile_options(blink_fonts_woff2_dec PRIVATE /wd4267)
endif()

set(_blink_fonts_ots_root
  "${BLINK_FONTS_CHROMIUM_ROOT}/third_party/ots/src")
add_library(blink_fonts_ots STATIC
  "${_blink_fonts_ots_root}/src/avar.cc"
  "${_blink_fonts_ots_root}/src/cff.cc"
  "${_blink_fonts_ots_root}/src/cff_charstring.cc"
  "${_blink_fonts_ots_root}/src/cmap.cc"
  "${_blink_fonts_ots_root}/src/cvar.cc"
  "${_blink_fonts_ots_root}/src/cvt.cc"
  "${_blink_fonts_ots_root}/src/fpgm.cc"
  "${_blink_fonts_ots_root}/src/fvar.cc"
  "${_blink_fonts_ots_root}/src/gasp.cc"
  "${_blink_fonts_ots_root}/src/gdef.cc"
  "${_blink_fonts_ots_root}/src/glyf.cc"
  "${_blink_fonts_ots_root}/src/gpos.cc"
  "${_blink_fonts_ots_root}/src/gsub.cc"
  "${_blink_fonts_ots_root}/src/gvar.cc"
  "${_blink_fonts_ots_root}/src/hdmx.cc"
  "${_blink_fonts_ots_root}/src/head.cc"
  "${_blink_fonts_ots_root}/src/hhea.cc"
  "${_blink_fonts_ots_root}/src/hvar.cc"
  "${_blink_fonts_ots_root}/src/kern.cc"
  "${_blink_fonts_ots_root}/src/layout.cc"
  "${_blink_fonts_ots_root}/src/loca.cc"
  "${_blink_fonts_ots_root}/src/ltsh.cc"
  "${_blink_fonts_ots_root}/src/math.cc"
  "${_blink_fonts_ots_root}/src/maxp.cc"
  "${_blink_fonts_ots_root}/src/metrics.cc"
  "${_blink_fonts_ots_root}/src/mvar.cc"
  "${_blink_fonts_ots_root}/src/name.cc"
  "${_blink_fonts_ots_root}/src/os2.cc"
  "${_blink_fonts_ots_root}/src/ots.cc"
  "${_blink_fonts_ots_root}/src/post.cc"
  "${_blink_fonts_ots_root}/src/prep.cc"
  "${_blink_fonts_ots_root}/src/stat.cc"
  "${_blink_fonts_ots_root}/src/variations.cc"
  "${_blink_fonts_ots_root}/src/vdmx.cc"
  "${_blink_fonts_ots_root}/src/vhea.cc"
  "${_blink_fonts_ots_root}/src/vorg.cc"
  "${_blink_fonts_ots_root}/src/vvar.cc")
target_include_directories(blink_fonts_ots PUBLIC
  "${_blink_fonts_ots_root}/include")
target_link_libraries(blink_fonts_ots PUBLIC
  blink_fonts_woff2_dec
  Brotli::brotlidec
  ZLIB::ZLIB)

unset(_blink_fonts_woff2_root)
unset(_blink_fonts_ots_root)
