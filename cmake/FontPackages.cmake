set(BLINK_FONTS_ICU_ROOT "${BLINK_FONTS_PACKAGES_ROOT}/icu_78.3-x64" CACHE PATH "ICU installation")
set(BLINK_FONTS_HARFBUZZ_ROOT "${BLINK_FONTS_PACKAGES_ROOT}/harfbuzz_14.2.0-x64" CACHE PATH "HarfBuzz installation")
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)
# The prebuilt packages only provide Debug and Release. Without a mapping,
# RelWithDebInfo picks the first listed configuration (Debug) and mismatches
# the /MD runtime and _ITERATOR_DEBUG_LEVEL.
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL Release)

foreach(_component IN ITEMS data uc i18n)
  if(_component STREQUAL "data")
    set(_basename sicudt)
  elseif(_component STREQUAL "uc")
    set(_basename sicuuc)
  else()
    set(_basename sicuin)
  endif()
  if(NOT TARGET ICU::${_component})
    add_library(ICU::${_component} STATIC IMPORTED GLOBAL)
    set_target_properties(ICU::${_component} PROPERTIES
      IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
      IMPORTED_LOCATION_DEBUG "${BLINK_FONTS_ICU_ROOT}/lib/${_basename}d.lib"
      IMPORTED_LOCATION_RELEASE "${BLINK_FONTS_ICU_ROOT}/lib/${_basename}.lib"
      MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE
      MAP_IMPORTED_CONFIG_MINSIZEREL RELEASE
      INTERFACE_INCLUDE_DIRECTORIES "${BLINK_FONTS_ICU_ROOT}/include"
      INTERFACE_COMPILE_DEFINITIONS U_STATIC_IMPLEMENTATION)
  endif()
endforeach()
# sicudtd.lib in this installation is ICU's 64-byte stub data. The release
# archive holds the complete read-only icudt78_dat resource with no CRT objects,
# so both configurations must link the same data archive.
set_property(TARGET ICU::data PROPERTY IMPORTED_LOCATION_DEBUG
  "${BLINK_FONTS_ICU_ROOT}/lib/sicudt.lib")
set_property(TARGET ICU::uc PROPERTY INTERFACE_LINK_LIBRARIES "ICU::data;advapi32")
set_property(TARGET ICU::i18n PROPERTY INTERFACE_LINK_LIBRARIES ICU::uc)
if(NOT TARGET ZLIB::ZLIB)
  find_package(ZLIB CONFIG REQUIRED PATHS
    "${BLINK_FONTS_PACKAGES_ROOT}/zlib_1.3.2-x64/lib/cmake/zlib" NO_DEFAULT_PATH)
endif()
find_package(PNG CONFIG REQUIRED PATHS
  "${BLINK_FONTS_PACKAGES_ROOT}/libpng_1.6.56-x64/lib/cmake/PNG" NO_DEFAULT_PATH)
find_package(freetype CONFIG REQUIRED PATHS
  "${BLINK_FONTS_PACKAGES_ROOT}/freetype_2.14.3-x64/lib/cmake/freetype" NO_DEFAULT_PATH)
find_package(harfbuzz CONFIG REQUIRED PATHS "${BLINK_FONTS_HARFBUZZ_ROOT}/lib/cmake/harfbuzz" NO_DEFAULT_PATH)
# This HarfBuzz package was built with FreeType support. Use FreeType's target
# rather than just its archive path so PNG, ZLIB and Brotli remain transitive.
# Font matching and glyph metrics in this port still use DirectWrite.
set_property(TARGET harfbuzz::harfbuzz PROPERTY INTERFACE_LINK_LIBRARIES
  "Freetype::Freetype;ICU::uc")
