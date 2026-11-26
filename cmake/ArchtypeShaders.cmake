# ---------------------------------------------------------------------------
# archtype_add_shaders(<target> <shader files...>)
#
# Compiles GLSL shaders (.vert, .frag, ...) to SPIR-V with glslc and copies
# the .spv files to a "shaders" folder next to the target's executable:
#
#   triangle.vert  ->  bin/shaders/triangle.vert.spv
#
# Shader paths are relative to the calling CMakeLists.txt.
# ---------------------------------------------------------------------------
function(archtype_add_shaders target)
    set(spv_files "")

    foreach(source IN LISTS ARGN)
        get_filename_component(source_path "${source}" ABSOLUTE)
        get_filename_component(source_name "${source}" NAME)
        set(output "${CMAKE_CURRENT_BINARY_DIR}/shaders/${source_name}.spv")

        add_custom_command(
            OUTPUT  "${output}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
            COMMAND Vulkan::glslc "${source_path}" -o "${output}"
            DEPENDS "${source_path}"
            COMMENT "Compiling shader ${source_name}"
            VERBATIM
        )

        list(APPEND spv_files "${output}")
    endforeach()

    # Runs on every build; copy_if_different only copies changed shaders.
    # TARGET_FILE_DIR also works with multi-config generators (Visual Studio).
    add_custom_target(${target}_shaders
        DEPENDS ${spv_files}
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${spv_files} "$<TARGET_FILE_DIR:${target}>/shaders"
        VERBATIM
    )

    add_dependencies(${target} ${target}_shaders)
endfunction()


# ---------------------------------------------------------------------------
# archtype_embed_shaders(<target> <shader files...>)
#
# Compiles GLSL shaders to SPIR-V as C arrays that the target's sources can
# #include, so the shaders live inside the binary instead of in .spv files:
#
#   src/renderer/shaders/renderer2d.vert
#     ->  #include "embedded/renderer2d.vert.inc"
#
#   static const uint32_t k_shader[] =
#   #include "embedded/renderer2d.vert.inc"
#   ;
#
# Shader paths are relative to the calling CMakeLists.txt.
# ---------------------------------------------------------------------------
function(archtype_embed_shaders target)
    set(include_dir "${CMAKE_CURRENT_BINARY_DIR}/${target}_shaders")
    set(outputs "")

    foreach(source IN LISTS ARGN)
        get_filename_component(source_path "${source}" ABSOLUTE)
        get_filename_component(source_name "${source}" NAME)
        set(output "${include_dir}/embedded/${source_name}.inc")

        # -mfmt=c writes the SPIR-V words as a C initializer list: {0x07230203, ...}
        add_custom_command(
            OUTPUT  "${output}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${include_dir}/embedded"
            COMMAND Vulkan::glslc -mfmt=c "${source_path}" -o "${output}"
            DEPENDS "${source_path}"
            COMMENT "Embedding shader ${source_name}"
            VERBATIM
        )

        list(APPEND outputs "${output}")
    endforeach()

    # Listing the generated files as sources makes CMake build them before
    # the C files that include them.
    target_sources(${target} PRIVATE ${outputs})
    target_include_directories(${target} PRIVATE "${include_dir}")
endfunction()