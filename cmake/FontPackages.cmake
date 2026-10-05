# Local implementation: prebuilt dependency discovery for the standalone font library.
if(WIN32)
  set(BLINK_FONTS_PACKAGES_ROOT "D:/MyPackages" CACHE PATH "Prebuilt dependencies")
else()
  set(BLINK_FONTS_PACKAGES_ROOT "$ENV{HOME}/MyPackages" CACHE PATH "Prebuilt dependencies")
endif()
set(BLINK_FONTS_ICU_ROOT "${BLINK_FONTS_PACKAGES_ROOT}/icu_78.3-x64" CACHE PATH "ICU installation")
set(BLINK_FONTS_HARFBUZZ_ROOT "${BLINK_FONTS_PACKAGES_ROOT}/harfbuzz_14.2.0-x64" CACHE PATH "HarfBuzz installation")
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)
# The prebuilt packages only provide Debug and Release. Without a mapping,
# RelWithDebInfo picks the first listed configuration (Debug) and mismatches
# the /MD runtime and _ITERATOR_DEBUG_LEVEL.
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL Release)

if(WIN32)
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
else()
  find_package(Threads REQUIRED)
  foreach(_component IN ITEMS data uc i18n)
    if(NOT TARGET ICU::${_component})
      add_library(ICU::${_component} STATIC IMPORTED GLOBAL)
      set_target_properties(ICU::${_component} PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION_DEBUG "${BLINK_FONTS_ICU_ROOT}/lib/libicu${_component}d.a"
        IMPORTED_LOCATION_RELEASE "${BLINK_FONTS_ICU_ROOT}/lib/libicu${_component}.a"
        INTERFACE_INCLUDE_DIRECTORIES "${BLINK_FONTS_ICU_ROOT}/include"
        INTERFACE_COMPILE_DEFINITIONS U_STATIC_IMPLEMENTATION)
    endif()
  endforeach()
  set_property(TARGET ICU::uc PROPERTY INTERFACE_LINK_LIBRARIES "ICU::data;Threads::Threads;${CMAKE_DL_LIBS};m")
  set_property(TARGET ICU::i18n PROPERTY INTERFACE_LINK_LIBRARIES ICU::uc)

  set(BLINK_FONTS_BROTLI_ROOT "${BLINK_FONTS_PACKAGES_ROOT}/brotli_1.2.0-x64" CACHE PATH "Brotli installation")
  foreach(_component IN ITEMS brotlicommon brotlidec)
    if(NOT TARGET Brotli::${_component})
      add_library(Brotli::${_component} STATIC IMPORTED GLOBAL)
      set_target_properties(Brotli::${_component} PROPERTIES
        IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
        IMPORTED_LOCATION_DEBUG "${BLINK_FONTS_BROTLI_ROOT}/lib/lib${_component}d.a"
        IMPORTED_LOCATION_RELEASE "${BLINK_FONTS_BROTLI_ROOT}/lib/lib${_component}.a"
        INTERFACE_INCLUDE_DIRECTORIES "${BLINK_FONTS_BROTLI_ROOT}/include")
    endif()
  endforeach()
  set_property(TARGET Brotli::brotlidec PROPERTY INTERFACE_LINK_LIBRARIES Brotli::brotlicommon)

  # Fontconfig is a platform service; discover it directly, without .pc files.
  if(NOT TARGET Fontconfig::Fontconfig)
    find_path(BLINK_FONTS_FONTCONFIG_INCLUDE_DIR fontconfig/fontconfig.h REQUIRED)
    find_library(BLINK_FONTS_FONTCONFIG_LIBRARY NAMES fontconfig REQUIRED)
    add_library(Fontconfig::Fontconfig UNKNOWN IMPORTED GLOBAL)
    set_target_properties(Fontconfig::Fontconfig PROPERTIES
      IMPORTED_LOCATION "${BLINK_FONTS_FONTCONFIG_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${BLINK_FONTS_FONTCONFIG_INCLUDE_DIR}")
  endif()
endif()
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
# bkfont also links FreeType directly for rasterization and metrics.
set_property(TARGET harfbuzz::harfbuzz PROPERTY INTERFACE_LINK_LIBRARIES
  "Freetype::Freetype;ICU::uc")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # Use targets instead of the absolute Brotli/FreeType archive paths exported
  # by these packages so every configuration retains transitive dependencies.
  set_property(TARGET freetype PROPERTY INTERFACE_LINK_LIBRARIES
    "ZLIB::ZLIB;PNG::PNG;Brotli::brotlidec")
  set_property(TARGET harfbuzz::harfbuzz APPEND PROPERTY INTERFACE_LINK_LIBRARIES Threads::Threads)
endif()
