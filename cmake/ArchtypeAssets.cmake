# ---------------------------------------------------------------------------
# archtype_add_assets(<target> <folder> <files...>)
#
# Copies asset files (images, models...) to <folder> next to the target's
# executable, so the game can load them with relative paths:
#
#   archtype_add_assets(game textures art/player.png)
#   ->  bin/textures/player.png
#
# File paths are relative to the calling CMakeLists.txt. Files are copied
# again whenever they change.
# ---------------------------------------------------------------------------
function(archtype_add_assets target folder)
    set(sources "")
    foreach(file IN LISTS ARGN)
        get_filename_component(path "${file}" ABSOLUTE)
        list(APPEND sources "${path}")
    endforeach()

    add_custom_target(${target}_assets_${folder}
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/${folder}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${sources} "$<TARGET_FILE_DIR:${target}>/${folder}"
        VERBATIM
    )

    add_dependencies(${target} ${target}_assets_${folder})
endfunction()