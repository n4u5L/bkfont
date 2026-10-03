# Use GoogleTest from the same Chromium checkout as the official Blink tests.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
set(GTEST_HAS_ABSL OFF CACHE BOOL "" FORCE)
add_subdirectory("${BLINK_FONTS_CHROMIUM_ROOT}/third_party/googletest/src"
                 "${CMAKE_CURRENT_BINARY_DIR}/googletest" EXCLUDE_FROM_ALL)

add_executable(blink_fonts_tests)
target_sources(blink_fonts_tests PRIVATE
  font/bitmap_glyphs_block_list_test.cc
  font/fallback_list_composite_key_test.cc
  font/font_cache_test.cc
  font/font_description_test.cc
  font/font_fallback_iterator_test.cc
  font/font_family_test.cc
  font/font_global_context_test.cc
  font/font_palette_test.cc
  font/font_platform_data_test.cc
  font/font_selection_types_test.cc
  font/font_size_adjust_test.cc
  font/font_test.cc
  font/font_test_utilities.cc
  font/font_test_utilities.h
  font/generic_font_family_settings_test.cc
  font/palette_interpolation_test.cc
  font/typesetting_features_test.cc
  font/unicode_range_set_test.cc
  font/web_font_typeface_factory_test.cc
  font/opentype/color_table_lookup_test.cc
  font/opentype/font_format_check_test.cc
  font/opentype/font_settings_test.cc
  font/opentype/open_type_cpal_lookup_test.cc
  font/opentype/variable_axes_names_test.cc
  shaping/caching_word_shaper_test.cc
  shaping/font_features_test.cc
  shaping/han_kerning_test.cc
  shaping/harfbuzz_face_test.cc
  shaping/harfbuzz_shaper_test.cc
  shaping/ng_shape_cache_test.cc
  shaping/orientation_iterator_test.cc
  shaping/run_segmenter_test.cc
  shaping/script_run_iterator_test.cc
  shaping/shape_result_run_test.cc
  shaping/shape_result_spacing_test.cc
  shaping/shape_result_test.cc
  shaping/shape_result_test_info.cc
  shaping/shape_result_test_info.h
  shaping/shape_result_view_test.cc
  shaping/small_caps_iterator_test.cc
  shaping/symbols_iterator_test.cc
  shaping/utf16_ragel_iterator_test.cc
  shaping/opentype/open_type_baseline_metrics_test.cc
  shaping/opentype/open_type_vertical_data_test.cc
  support/font_test_base.h
  support/font_test_helpers.cc
  support/font_test_helpers.h
  support/runtime_enabled_features_test_helpers.h
  support/test_main.cc
  support/wtf_test_helper.cc
  support/wtf_test_helper.h
  text/bidi_paragraph_test.cc
  text/character_test.cc
  text/icu_error_test.cc
  text/layout_locale_test.cc
  text/text_break_iterator_test.cc
  text/text_run_test.cc
  base/casting_test.cc
  base/deque_test.cc
  base/dtoa_test.cc
  base/hash_map_test.cc
  base/hash_set_test.cc
  base/linked_hash_set_test.cc
  base/math_extras_test.cc
  base/type_traits_test.cc
  base/vector_backed_linked_list_test.cc
  base/vector_test.cc
  base/allocator/allocator_test.cc
  base/allocator/partitions_test.cc
  base/text/ascii_ctype_test.cc
  base/text/atomic_string_table_test.cc
  base/text/atomic_string_test.cc
  base/text/case_map_test.cc
  base/text/code_point_iterator_test.cc
  base/text/integer_to_string_conversion_test.cc
  base/text/strcat_test.cc
  base/text/string_buffer_test.cc
  base/text/string_builder_test.cc
  base/text/string_hasher_test.cc
  base/text/string_impl_test.cc
  base/text/string_to_number_test.cc
  base/text/string_view_test.cc
  base/text/text_offset_map_test.cc
  base/text/wtf_string_test.cc)
target_include_directories(blink_fonts_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/support"
  "${PROJECT_BINARY_DIR}/tools/character_data/generated")
target_compile_definitions(blink_fonts_tests PRIVATE
  CHROMIUM_SOURCE_DIR="${BLINK_FONTS_CHROMIUM_ROOT}")
target_link_libraries(blink_fonts_tests PRIVATE blink_fonts GTest::gmock)
add_test(NAME blink_fonts_official_tests
  COMMAND blink_fonts_tests
    "--gtest_output=xml:${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/blink_fonts_tests.xml")
set_tests_properties(blink_fonts_official_tests PROPERTIES TIMEOUT 300)
