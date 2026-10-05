# Ported from: blink/renderer/platform/BUILD.gn
# Local CMake adaptation of the character_data and character_data_generator targets.
cmake_minimum_required(VERSION 3.24)
project(blink_font_character_data LANGUAGES CXX)
if(NOT TARGET ICU::i18n)
  if(WIN32)
    set(BLINK_FONTS_PACKAGES_ROOT "D:/MyPackages" CACHE PATH "Prebuilt MSVC x64 dependencies")
  endif()
  include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/NoExceptions.cmake")
  include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/FontPackages.cmake")
endif()
add_executable(character_data_generator)
target_sources(character_data_generator PRIVATE
  character_names.h
  character_property.h
  character_property_data.h
  character_property_data_generator.cc
  east_asian_spacing_type.h
  han_kerning_char_type.h
  wtf_uchar.h)
target_compile_features(character_data_generator PRIVATE cxx_std_20)
if(MSVC)
  target_compile_definitions(character_data_generator PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
target_link_libraries(character_data_generator PRIVATE ICU::i18n ICU::uc)
set(GENERATED_CHARACTER_DATA "${CMAKE_CURRENT_BINARY_DIR}/generated/character_property_data.cc")
set(GENERATED_BREAK_DATA "${CMAKE_CURRENT_BINARY_DIR}/generated/text/break_iterator_data_inline_header.h")
add_custom_command(OUTPUT "${GENERATED_CHARACTER_DATA}" "${GENERATED_BREAK_DATA}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated/text"
  COMMAND $<TARGET_FILE:character_data_generator> "${GENERATED_CHARACTER_DATA}" "${GENERATED_BREAK_DATA}"
  DEPENDS character_data_generator
  VERBATIM)
add_custom_target(generate_character_data DEPENDS "${GENERATED_CHARACTER_DATA}" "${GENERATED_BREAK_DATA}")
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(GENERATED_CHARACTER_DATA "${GENERATED_CHARACTER_DATA}" PARENT_SCOPE)
endif()
