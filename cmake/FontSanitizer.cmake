# Ported from: chromium/third_party/ots/BUILD.gn
# Ported from: chromium/third_party/woff2/BUILD.gn
# Local CMake adaptation of the OTS and woff2_dec source lists.
include_guard(GLOBAL)

if(NOT DEFINED BKIT_CHROMIUM_ROOT)
  message(FATAL_ERROR "BKIT_CHROMIUM_ROOT must name the Chromium source root")
endif()
if(WIN32 AND NOT DEFINED BKIT_PACKAGES_ROOT)
  message(FATAL_ERROR "BKIT_PACKAGES_ROOT must name the prebuilt package directory")
endif()

if(NOT TARGET ZLIB::ZLIB)
  find_package(ZLIB CONFIG REQUIRED
    PATHS "${BKIT_PACKAGES_ROOT}/zlib_1.3.2-x64/lib/cmake/zlib"
    NO_DEFAULT_PATH)
endif()
if(NOT TARGET Brotli::brotlidec)
  find_package(Brotli CONFIG REQUIRED
    PATHS "${BKIT_PACKAGES_ROOT}/brotli_1.2.0-x64/lib/cmake/Brotli"
    NO_DEFAULT_PATH)
endif()

# The installed packages contain separate Debug and Release static libraries.
if(WIN32)
  set_target_properties(ZLIB::ZLIB PROPERTIES
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE
    MAP_IMPORTED_CONFIG_MINSIZEREL RELEASE)
endif()

set(_bkit_woff2_root
  "${BKIT_CHROMIUM_ROOT}/third_party/woff2")
add_library(bkit_woff2_dec STATIC
  "${_bkit_woff2_root}/src/table_tags.cc"
  "${_bkit_woff2_root}/src/variable_length.cc"
  "${_bkit_woff2_root}/src/woff2_common.cc"
  "${_bkit_woff2_root}/src/woff2_dec.cc"
  "${_bkit_woff2_root}/src/woff2_out.cc")
add_library(bkit::woff2_dec ALIAS bkit_woff2_dec)
target_include_directories(bkit_woff2_dec PUBLIC
  "${_bkit_woff2_root}/include")
target_link_libraries(bkit_woff2_dec PUBLIC Brotli::brotlidec)
if(MSVC)
  # Matches the Windows warning option in Chromium's woff2_dec target.
  target_compile_options(bkit_woff2_dec PRIVATE /wd4267)
endif()

set(_bkit_ots_root
  "${BKIT_CHROMIUM_ROOT}/third_party/ots/src")
add_library(bkit_ots STATIC
  "${_bkit_ots_root}/src/avar.cc"
  "${_bkit_ots_root}/src/cff.cc"
  "${_bkit_ots_root}/src/cff_charstring.cc"
  "${_bkit_ots_root}/src/cmap.cc"
  "${_bkit_ots_root}/src/cvar.cc"
  "${_bkit_ots_root}/src/cvt.cc"
  "${_bkit_ots_root}/src/fpgm.cc"
  "${_bkit_ots_root}/src/fvar.cc"
  "${_bkit_ots_root}/src/gasp.cc"
  "${_bkit_ots_root}/src/gdef.cc"
  "${_bkit_ots_root}/src/glyf.cc"
  "${_bkit_ots_root}/src/gpos.cc"
  "${_bkit_ots_root}/src/gsub.cc"
  "${_bkit_ots_root}/src/gvar.cc"
  "${_bkit_ots_root}/src/hdmx.cc"
  "${_bkit_ots_root}/src/head.cc"
  "${_bkit_ots_root}/src/hhea.cc"
  "${_bkit_ots_root}/src/hvar.cc"
  "${_bkit_ots_root}/src/kern.cc"
  "${_bkit_ots_root}/src/layout.cc"
  "${_bkit_ots_root}/src/loca.cc"
  "${_bkit_ots_root}/src/ltsh.cc"
  "${_bkit_ots_root}/src/math.cc"
  "${_bkit_ots_root}/src/maxp.cc"
  "${_bkit_ots_root}/src/metrics.cc"
  "${_bkit_ots_root}/src/mvar.cc"
  "${_bkit_ots_root}/src/name.cc"
  "${_bkit_ots_root}/src/os2.cc"
  "${_bkit_ots_root}/src/ots.cc"
  "${_bkit_ots_root}/src/post.cc"
  "${_bkit_ots_root}/src/prep.cc"
  "${_bkit_ots_root}/src/stat.cc"
  "${_bkit_ots_root}/src/variations.cc"
  "${_bkit_ots_root}/src/vdmx.cc"
  "${_bkit_ots_root}/src/vhea.cc"
  "${_bkit_ots_root}/src/vorg.cc"
  "${_bkit_ots_root}/src/vvar.cc")
add_library(bkit::ots ALIAS bkit_ots)
target_include_directories(bkit_ots PUBLIC
  "${_bkit_ots_root}/include")
target_link_libraries(bkit_ots PUBLIC
  bkit::woff2_dec
  Brotli::brotlidec
  ZLIB::ZLIB)

unset(_bkit_woff2_root)
unset(_bkit_ots_root)
