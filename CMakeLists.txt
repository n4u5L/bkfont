# Local implementation: standalone font library build configuration.
cmake_minimum_required(VERSION 3.28)
project(blink_fonts LANGUAGES CXX)
if(DEFINED BLINK_FONTS_SHOWINCLUDES_PREFIX)
  # CMake's compiler probe may misdecode UTF-8 diagnostics from a localized
  # MSVC installation. Ninja needs the exact prefix for header dependencies.
  set(CMAKE_CL_SHOWINCLUDES_PREFIX "${BLINK_FONTS_SHOWINCLUDES_PREFIX}")
endif()

if(WIN32)
  if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The Windows font port requires MSVC for Windows x64")
  endif()
elseif(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "The font port supports Windows and Linux")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
if(WIN32)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  set(BLINK_FONTS_PACKAGES_ROOT "D:/MyPackages" CACHE PATH "Prebuilt MSVC x64 dependencies")
endif()
get_filename_component(_chromium_default "${CMAKE_CURRENT_SOURCE_DIR}/../../../Qt5/qtwebengine/src/3rdparty/chromium" ABSOLUTE)
set(BLINK_FONTS_CHROMIUM_ROOT "${_chromium_default}" CACHE PATH "Source Chromium checkout")

if(MSVC)
  add_compile_options(/utf-8 /Zc:__cplusplus /permissive- /bigobj)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()

include(cmake/NoExceptions.cmake)
include(cmake/FontPackages.cmake)
include(cmake/FontSanitizer.cmake)
add_subdirectory(tools/character_data)

add_library(blink_fonts STATIC)
add_library(blink_fonts::blink_fonts ALIAS blink_fonts)
add_dependencies(blink_fonts generate_character_data)
target_sources(blink_fonts PRIVATE
  src/base/base_export.h
  src/base/bit_cast.h
  src/base/compiler_specific.h
  src/base/gtest_prod_util.h
  src/base/immediate_crash.h
  src/base/containers/adapters.h
  src/base/containers/adapters_internal.h
  src/base/containers/flat_map.h
  src/base/containers/flat_tree.h
  src/base/containers/heap_array.h
  src/base/containers/span.h
  src/base/containers/span_forward_internal.h
  src/base/functional/callback.h
  src/base/functional/callback_forward.h
  src/base/functional/function_ref.h
  src/base/hash/hash.h src/base/hash/hash.cc
  src/base/i18n/base_i18n_export.h
  src/base/i18n/rtl.h
  src/base/i18n/string_search.h src/base/i18n/string_search.cc
  src/base/memory/ptr_util.h
  src/base/memory/raw_ptr.h
  src/base/memory/raw_ptr_exclusion.h
  src/base/memory/raw_span.h
  src/base/memory/ref_counted.h
  src/base/memory/scoped_refptr.h
  src/base/memory/stack_allocated.h
  src/base/memory/values_equivalent.h
  src/base/numerics/angle_conversions.h
  src/base/numerics/basic_ops_impl.h
  src/base/numerics/byte_conversions.h
  src/base/numerics/checked_math.h
  src/base/numerics/checked_math_impl.h
  src/base/numerics/clamped_math.h
  src/base/numerics/clamped_math_impl.h
  src/base/numerics/integral_constant_like.h
  src/base/numerics/safe_conversions.h
  src/base/numerics/safe_conversions_arm_impl.h
  src/base/numerics/safe_conversions_impl.h
  src/base/numerics/safe_math_arm_impl.h
  src/base/numerics/safe_math_clang_gcc_impl.h
  src/base/numerics/safe_math_shared_impl.h
  src/base/strings/span_printf.h
  src/base/strings/string_util.h
  src/base/strings/string_util_internal.h
  src/base/strings/string_util_posix.h
  src/base/strings/string_util_win.h
  src/base/strings/string_view_util.h
  src/base/synchronization/lock.h
  src/base/third_party/cityhash/city.h src/base/third_party/cityhash/city.cc
  src/base/third_party/double_conversion/double-conversion/bignum.h src/base/third_party/double_conversion/double-conversion/bignum.cc
  src/base/third_party/double_conversion/double-conversion/bignum-dtoa.h src/base/third_party/double_conversion/double-conversion/bignum-dtoa.cc
  src/base/third_party/double_conversion/double-conversion/cached-powers.h src/base/third_party/double_conversion/double-conversion/cached-powers.cc
  src/base/third_party/double_conversion/double-conversion/diy-fp.h
  src/base/third_party/double_conversion/double-conversion/double-conversion.h
  src/base/third_party/double_conversion/double-conversion/double-to-string.h src/base/third_party/double_conversion/double-conversion/double-to-string.cc
  src/base/third_party/double_conversion/double-conversion/fast-dtoa.h src/base/third_party/double_conversion/double-conversion/fast-dtoa.cc
  src/base/third_party/double_conversion/double-conversion/fixed-dtoa.h src/base/third_party/double_conversion/double-conversion/fixed-dtoa.cc
  src/base/third_party/double_conversion/double-conversion/ieee.h
  src/base/third_party/double_conversion/double-conversion/string-to-double.h src/base/third_party/double_conversion/double-conversion/string-to-double.cc
  src/base/third_party/double_conversion/double-conversion/strtod.h src/base/third_party/double_conversion/double-conversion/strtod.cc
  src/base/third_party/double_conversion/double-conversion/utils.h
  src/base/threading/platform_thread.h
  src/base/threading/thread_local_storage.h
  src/base/types/pass_key.h
  src/base/types/strong_alias.h
  src/base/types/supports_ostream_operator.h
  src/base/types/to_address.h
  src/base/types/zip.h
  src/base/casting.h
  src/base/deque.h
  src/base/dtoa.h src/base/dtoa.cc
  src/base/dynamic_annotations.h src/base/dynamic_annotations.cc
  src/base/forward.h
  src/base/get_ptr.h
  src/base/hash_functions.h
  src/base/hash_map.h
  src/base/hash_set.h
  src/base/hash_table.h
  src/base/hash_table_deleted_value_type.h
  src/base/hash_traits.h
  src/base/key_value_pair.h
  src/base/leak_annotations.h
  src/base/lean_windows.h
  src/base/linked_hash_set.h
  src/base/math_extras.h
  src/base/mutex.h
  src/base/notreached.h src/base/notreached.cc
  src/base/once.h
  src/base/ref_counted.h
  src/base/sanitizers.h
  src/base/semaphore.h src/base/semaphore.cc
  src/base/stack_util.h src/base/stack_util.cc
  src/base/static_constructors.h
  src/base/std_lib_extras.h
  src/base/thread_safe_ref_counted.h
  src/base/thread_id.h src/base/thread_id.cc
  src/base/thread_specific.h
  src/base/threading.h src/base/threading.cc
  src/base/type_traits.h
  src/base/vector.h
  src/base/vector_backed_linked_list.h
  src/base/vector_traits.h
  src/base/wtf.h src/base/wtf.cc
  src/base/wtf_size_t.h
  src/base/allocator/allocator.h
  src/base/allocator/partition_allocator.h src/base/allocator/partition_allocator.cc
  src/base/allocator/partitions.h src/base/allocator/partitions.cc
  src/base/text/ascii_ctype.h
  src/base/text/ascii_fast_path.h
  src/base/text/atomic_string.h src/base/text/atomic_string.cc
  src/base/text/atomic_string_encoding.h
  src/base/text/atomic_string_hash.h
  src/base/text/atomic_string_table.h src/base/text/atomic_string_table.cc
  src/base/text/case_folding_hash.h
  src/base/text/case_map.h src/base/text/case_map.cc
  src/base/text/character_names.h
  src/base/text/character_visitor.h
  src/base/text/code_point_iterator.h
  src/base/text/convert_to_8bit_hash_reader.h
  src/base/text/copy_lchars_from_uchar_source.h
  src/base/text/integer_to_string_conversion.h
  src/base/text/number_parsing_options.h
  src/base/text/strcat.h src/base/text/strcat.cc
  src/base/text/string_buffer.h
  src/base/text/string_builder.h src/base/text/string_builder.cc
  src/base/text/string_concatenate.h src/base/text/string_concatenate.cc
  src/base/text/string_hash.h
  src/base/text/string_hasher.h
  src/base/text/string_impl.h src/base/text/string_impl.cc
  src/base/text/string_operators.h
  src/base/text/string_operators_atomic.h
  src/base/text/string_statics.h src/base/text/string_statics.cc
  src/base/text/string_to_number.h src/base/text/string_to_number.cc
  src/base/text/string_utf8_adaptor.h src/base/text/string_utf8_adaptor.cc
  src/base/text/string_view.h src/base/text/string_view.cc
  src/base/text/text_offset_map.h src/base/text/text_offset_map.cc
  src/base/text/unicode.h
  src/base/text/unicode_string.h
  src/base/text/utf16.h
  src/base/text/utf8.h src/base/text/utf8.cc
  src/base/text/wtf_string.h src/base/text/wtf_string.cc
  src/base/text/wtf_uchar.h
  src/build/build_config.h
  src/third_party/rapidhash/rapidhash.h
  src/fonts.h
  src/initialize.h src/initialize.cc
  src/language.h src/language.cc
  src/platform_export.h
  src/runtime_enabled_features.h
  src/base/third_party/superfasthash/superfasthash.cc
  src/font/alternate_font_family.h
  src/font/canvas_rotation_in_vertical.h
  src/font/character_range.h
  src/font/canvas_paint_canvas.h src/font/canvas_paint_canvas.cc
  src/font/color_table_lookup.h src/font/color_table_lookup.cc
  src/font/custom_font_data.h
  src/font/fallback_list_composite_key.h
  src/font/font.h src/font/font.cc
  src/font/font_baseline.h
  src/font/font_cache.h src/font/font_cache.cc
  src/font/font_cache_client.h src/font/font_cache_client.cc
  src/font/font_cache_key.h
  src/font/font_custom_platform_data.h src/font/font_custom_platform_data.cc
  src/font/font_data.h
  src/font/font_data_cache.h src/font/font_data_cache.cc
  src/font/font_data_for_range_set.h src/font/font_data_for_range_set.cc
  src/font/font_description.h src/font/font_description.cc
  src/font/font_face_creation_params.h
  src/font/font_fallback_iterator.h src/font/font_fallback_iterator.cc
  src/font/font_fallback_list.h src/font/font_fallback_list.cc
  src/font/font_fallback_map.h src/font/font_fallback_map.cc
  src/font/font_fallback_priority.h
  src/font/font_family.h src/font/font_family.cc
  src/font/font_family_names.h src/font/font_family_names.cc
  src/font/font_format_check.h src/font/font_format_check.cc
  src/font/font_height.h src/font/font_height.cc
  src/font/font_invalidation_reason.h
  src/font/font_metrics.h src/font/font_metrics.cc
  src/font/font_metrics_override.h
  src/font/font_optical_sizing.h src/font/font_optical_sizing.cc
  src/font/font_orientation.h src/font/font_orientation.cc
  src/font/font_palette.h src/font/font_palette.cc
  src/font/font_platform_data.h src/font/font_platform_data.cc
  src/font/font_platform_data_cache.h src/font/font_platform_data_cache.cc
  src/font/font_prewarmer.h
  src/font/font_render_style.h
  src/font/font_selection_algorithm.h src/font/font_selection_algorithm.cc
  src/font/font_selection_types.h src/font/font_selection_types.cc
  src/font/font_selector.h src/font/font_selector.cc
  src/font/font_selector_client.h
  src/font/font_settings.h src/font/font_settings.cc
  src/font/font_size_adjust.h src/font/font_size_adjust.cc
  src/font/font_smoothing_mode.h src/font/font_smoothing_mode.cc
  src/font/font_variant_alternates.h src/font/font_variant_alternates.cc
  src/font/font_variant_east_asian.h src/font/font_variant_east_asian.cc
  src/font/font_variant_emoji.h src/font/font_variant_emoji.cc
  src/font/font_variant_numeric.h src/font/font_variant_numeric.cc
  src/font/font_vertical_position_type.h
  src/font/font_width_variant.h src/font/font_width_variant.cc
  src/font/generic_font_family_settings.h src/font/generic_font_family_settings.cc
  src/font/glyph.h
  src/font/open_type_cpal_lookup.h src/font/open_type_cpal_lookup.cc
  src/font/paint_canvas.h
  src/font/palette_interpolation.h src/font/palette_interpolation.cc
  src/font/plain_text_node.h src/font/plain_text_node.cc
  src/font/resolved_font_features.h src/font/resolved_font_features.cc
  src/font/segmented_font_data.h src/font/segmented_font_data.cc
  src/font/simple_font_data.h src/font/simple_font_data.cc
  src/font/string_truncator.h src/font/string_truncator.cc
  src/font/target_platform.h
  src/font/text_fragment_paint_info.h src/font/text_fragment_paint_info.cc
  src/font/text_metrics.h src/font/text_metrics.cc
  src/font/text_rendering_mode.h src/font/text_rendering_mode.cc
  src/font/text_run_paint_info.h
  src/font/typesetting_features.h src/font/typesetting_features.cc
  src/font/unicode_range_set.h src/font/unicode_range_set.cc
  src/font/value_equivalent.h
  src/font/variable_axes_names.h src/font/variable_axes_names.cc
  src/font/vdmx_parser.h src/font/vdmx_parser.cc
  src/font/web_font_decoder.h src/font/web_font_decoder.cc
  src/geometry/blend.h
  src/geometry/calculation_expression_node.h src/geometry/calculation_expression_node.cc
  src/geometry/calculation_value.h src/geometry/calculation_value.cc
  src/geometry/color_channel_keyword.h
  src/geometry/evaluation_input.h
  src/geometry/length.h src/geometry/length.cc
  src/geometry/length_functions.h src/geometry/length_functions.cc
  src/geometry/length_point.h
  src/geometry/length_size.h
  src/geometry/math_functions.h
  src/graphics/color.h src/graphics/color.cc
  src/graphics/color_conversions.h src/graphics/color_conversions.cc
  src/graphics/color_float.h
  src/graphics/color_math.h src/graphics/color_math.cc
  src/graphics/color_names.cc
  src/graphics/sin_cos_degrees.h
  src/platform/arena.h
  src/platform/bezier_curves.h src/platform/bezier_curves.cc
  src/platform/blend_mode.h src/platform/blend_mode.cc
  src/platform/canvas.h src/platform/canvas.cc
  src/platform/color4f.h
  src/platform/data.h src/platform/data.cc
  src/platform/font_arguments.h
  src/platform/font_descriptor.h src/platform/font_descriptor.cc
  src/platform/font_host_freetype.cc
  src/platform/font_host_freetype_common.h src/platform/font_host_freetype_common.cc
  src/platform/font_manager.h src/platform/font_manager.cc
  src/platform/font_manager_custom.h src/platform/font_manager_custom.cc
  src/platform/font_parameters.h
  src/platform/font_style.h
  src/platform/glyph_run.h src/platform/glyph_run.cc
  src/platform/glyph_run_painter.h src/platform/glyph_run_painter.cc
  src/platform/mask.h src/platform/mask.cc
  src/platform/mask_gamma.h src/platform/mask_gamma.cc
  src/platform/matrix.h src/platform/matrix.cc
  src/platform/opentype_name.cc
  src/platform/opentype_svg_decoder.h src/platform/opentype_svg_decoder.cc
  src/platform/ot_utils.h src/platform/ot_utils.cc
  src/platform/path.h src/platform/path.cc
  src/platform/path_rasterizer.h src/platform/path_rasterizer.cc
  src/platform/picture.h src/platform/picture.cc
  src/platform/pixmap.h src/platform/pixmap.cc
  src/platform/platform_font.h src/platform/platform_font.cc
  src/platform/platform_font_metrics.h
  src/platform/platform_glyph.h src/platform/platform_glyph.cc
  src/platform/platform_paint.h
  src/platform/raster_canvas.h src/platform/raster_canvas.cc
  src/platform/rect.h
  src/platform/scalar.h
  src/platform/scaler_context.h src/platform/scaler_context.cc
  src/platform/shader.h src/platform/shader.cc
  src/platform/stream.h src/platform/stream.cc
  src/platform/strike.h src/platform/strike.cc
  src/platform/strike_cache.h src/platform/strike_cache.cc
  src/platform/strike_spec.h src/platform/strike_spec.cc
  src/platform/surface_props.h
  src/platform/text_blob.h src/platform/text_blob.cc
  src/platform/typeface.h src/platform/typeface.cc
  src/platform/typeface_cache.h src/platform/typeface_cache.cc
  src/platform/typeface_freetype.h
  src/platform/typeface_proxy.h src/platform/typeface_proxy.cc
  src/shaping/caching_word_shape_iterator.h src/shaping/caching_word_shape_iterator.cc
  src/shaping/caching_word_shaper.h src/shaping/caching_word_shaper.cc
  src/shaping/case_mapping_harfbuzz_buffer_filler.h src/shaping/case_mapping_harfbuzz_buffer_filler.cc
  src/shaping/emoji_presentation_scanner.c
  src/shaping/font_features.h src/shaping/font_features.cc
  src/shaping/frame_shape_cache.h src/shaping/frame_shape_cache.cc
  src/shaping/glyph_bounds_accumulator.h
  src/shaping/glyph_data.h
  src/shaping/glyph_data_range.h src/shaping/glyph_data_range.cc
  src/shaping/glyph_index_result.h
  src/shaping/glyph_offset_iterator.h
  src/shaping/han_kerning.h src/shaping/han_kerning.cc
  src/shaping/harfbuzz_face.h src/shaping/harfbuzz_face.cc
  src/shaping/harfbuzz_face_from_typeface.h src/shaping/harfbuzz_face_from_typeface.cc
  src/shaping/harfbuzz_font_cache.h src/shaping/harfbuzz_font_cache.cc
  src/shaping/harfbuzz_font_data.h
  src/shaping/harfbuzz_shaper.h src/shaping/harfbuzz_shaper.cc
  src/shaping/ng_shape_cache.h
  src/shaping/opentype/open_type_baseline_metrics.h src/shaping/opentype/open_type_baseline_metrics.cc
  src/shaping/opentype/open_type_caps_support.h src/shaping/opentype/open_type_caps_support.cc
  src/shaping/opentype/open_type_caps_support_mpl.cc
  src/shaping/opentype/open_type_features.h src/shaping/opentype/open_type_features.cc
  src/shaping/opentype/open_type_math_stretch_data.h
  src/shaping/opentype/open_type_math_support.h src/shaping/opentype/open_type_math_support.cc
  src/shaping/opentype/open_type_types.h
  src/shaping/opentype/open_type_vertical_data.h src/shaping/opentype/open_type_vertical_data.cc
  src/shaping/orientation_iterator.h src/shaping/orientation_iterator.cc
  src/shaping/run_segmenter.h src/shaping/run_segmenter.cc
  src/shaping/script_run_iterator.h src/shaping/script_run_iterator.cc
  src/shaping/shape_cache.h
  src/shaping/shape_options.h
  src/shaping/shape_result.h src/shaping/shape_result.cc
  src/shaping/shape_result_bloberizer.h src/shaping/shape_result_bloberizer.cc
  src/shaping/shape_result_buffer.h src/shaping/shape_result_buffer.cc
  src/shaping/shape_result_cursor.h src/shaping/shape_result_cursor.cc
  src/shaping/shape_result_run.h
  src/shaping/shape_result_spacing.h src/shaping/shape_result_spacing.cc
  src/shaping/shape_result_view.h src/shaping/shape_result_view.cc
  src/shaping/shaping_line_breaker.h src/shaping/shaping_line_breaker.cc
  src/shaping/small_caps_iterator.h src/shaping/small_caps_iterator.cc
  src/shaping/stretchy_operator_shaper.h src/shaping/stretchy_operator_shaper.cc
  src/shaping/support/gfx/geometry.h
  src/shaping/support/gfx/geometry/rect_f.h
  src/shaping/support/layout_unit.h src/shaping/support/layout_unit.cc
  src/shaping/symbols_iterator.h src/shaping/symbols_iterator.cc
  src/shaping/text_spacing_trim.h
  src/shaping/utf16_ragel_iterator.h
  src/shaping/utf16_text_iterator.h src/shaping/utf16_text_iterator.cc
  src/shaping/variation_selector_mode.h src/shaping/variation_selector_mode.cc
  src/text/bidi_paragraph.h src/text/bidi_paragraph.cc
  src/text/character.h src/text/character.cc
  src/text/character_break_iterator.h src/text/character_break_iterator.cc
  src/text/character_emoji.cc
  src/text/character_property.h
  src/text/character_property_data.h
  src/text/character_variation_sequences.cc
  src/text/east_asian_spacing_type.h
  src/text/emoji_segmentation_category.h
  src/text/emoji_segmentation_category_inline_header.h
  src/text/han_kerning_char_type.h
  src/text/hyphenation.h src/text/hyphenation.cc
  src/text/icu_error.h src/text/icu_error.cc
  src/text/layout_locale.h src/text/layout_locale.cc
  src/text/locale_to_script_mapping.h src/text/locale_to_script_mapping.cc
  src/text/quotes_data.h src/text/quotes_data.cc
  src/text/tab_size.h
  src/text/text_break_iterator.h src/text/text_break_iterator.cc
  src/text/text_break_iterator_icu.cc
  src/text/text_break_iterator_internal_icu.h src/text/text_break_iterator_internal_icu.cc
  src/text/text_direction.h src/text/text_direction.cc
  src/text/text_run.h src/text/text_run.cc
  "${GENERATED_CHARACTER_DATA}")
# Ragel output textually included by symbols_iterator.cc.
set_source_files_properties(src/shaping/emoji_presentation_scanner.c
  PROPERTIES HEADER_FILE_ONLY ON)
target_include_directories(blink_fonts
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
  PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/tools/character_data/generated"
          "${CMAKE_CURRENT_SOURCE_DIR}/src/base/third_party/double_conversion/double-conversion")
target_compile_features(blink_fonts PUBLIC cxx_std_20)
target_link_libraries(blink_fonts
  PUBLIC harfbuzz::harfbuzz ICU::i18n ICU::uc
  PRIVATE blink_fonts_ots Freetype::Freetype)
if(WIN32)
  target_sources(blink_fonts PRIVATE
    src/base/threading/thread_local_storage_win.cc
    src/font/font_fallback_win.h src/font/font_fallback_win.cc
    src/font/font_unique_name_lookup_win.h src/font/font_unique_name_lookup_win.cc
    src/platform/dwrite_font_file_stream.h src/platform/dwrite_font_file_stream.cc
    src/platform/dwrite_internal.h src/platform/dwrite_internal.cc
    src/platform/font_manager_win.h src/platform/font_manager_win.cc)
  target_compile_definitions(blink_fonts PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(blink_fonts PRIVATE dwrite user32)
else()
  target_sources(blink_fonts PRIVATE
    src/base/threading/thread_local_storage_posix.h
    src/base/threading/thread_local_storage_posix.cc
    src/font/font_cache_linux.cc
    src/font/font_fallback_linux.h src/font/font_fallback_linux.cc
    src/font/font_unique_name_lookup_linux.h src/font/font_unique_name_lookup_linux.cc
    src/platform/fontconfig_util.h src/platform/fontconfig_util.cc
    src/platform/font_manager_fontconfig.h src/platform/font_manager_fontconfig.cc)
  target_link_libraries(blink_fonts PRIVATE Fontconfig::Fontconfig Threads::Threads)
endif()

option(BLINK_FONTS_BUILD_EXAMPLES "Build the GLFW text rendering example" ON)
if(BLINK_FONTS_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
