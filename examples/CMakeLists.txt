# C++ is only used here, to check that the engine's C headers work from C++
enable_language(CXX)

# C example
add_executable(hello_c c/hello/main.c)
target_link_libraries(hello_c PRIVATE Archtype::archtype)

# C++ example (uses the same C headers through extern "C")
add_executable(hello_cpp cpp/hello/main.cpp)
target_link_libraries(hello_cpp PRIVATE Archtype::archtype)

# Window and main loop example
add_executable(window_c c/window/main.c)
target_link_libraries(window_c PRIVATE Archtype::archtype)

# Renderer example (Vulkan instance and validation layers)
add_executable(renderer_c c/renderer/main.c)
target_link_libraries(renderer_c PRIVATE Archtype::archtype)

# First triangle (shaders and graphics pipeline)
add_executable(triangle_c c/triangle/main.c)
target_link_libraries(triangle_c PRIVATE Archtype::archtype)
archtype_add_shaders(triangle_c
    c/triangle/triangle.vert
    c/triangle/triangle.frag
)

# Quad drawn from vertex and index buffers
add_executable(quad_c c/quad/main.c)
target_link_libraries(quad_c PRIVATE Archtype::archtype)
archtype_add_shaders(quad_c
    c/quad/quad.vert
    c/quad/quad.frag
)

# Paint: draw a color-changing trail with the mouse
add_executable(paint_c c/paint/main.c)
target_link_libraries(paint_c PRIVATE Archtype::archtype)
archtype_add_shaders(paint_c
    c/paint/brush.vert
    c/paint/brush.frag
)

# Spinning quads in 3D: uniform buffer (camera) and push constants (per object)
add_executable(spin_c c/spin/main.c)
target_link_libraries(spin_c PRIVATE Archtype::archtype)
archtype_add_shaders(spin_c
    c/spin/spin.vert
    c/spin/spin.frag
)

# Textures: a generated checkerboard and an image loaded from a file.
# Put any PNG/JPG at c/texture/image.png and run CMake again to use it.
add_executable(texture_c c/texture/main.c)
target_link_libraries(texture_c PRIVATE Archtype::archtype)
archtype_add_shaders(texture_c
    c/texture/texture.vert
    c/texture/texture.frag
)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/c/texture/image.png")
    archtype_add_assets(texture_c textures c/texture/image.png)
endif()