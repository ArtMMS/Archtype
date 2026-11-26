cmake_minimum_required(VERSION 3.20)

project(Archtype
    VERSION 0.1.0
    DESCRIPTION "A 2D/3D game engine written in C with Vulkan"
    LANGUAGES C # The engine is pure C; C++ is enabled only for the C++ example
)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(ARCHTYPE_BUILD_EXAMPLES "Build the example programs" ON)
option(ARCHTYPE_BUILD_TESTS "Build the tests" ON)
option(BUILD_SHARED_LIBS "Build Archtype as a shared library" OFF)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
# Strict C99 (-std=c99, not gnu99). MSVC has no C99 mode and ignores this.
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Only used by the C++ example (examples/cpp)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Debug for single-config generators (Makefiles, Ninja)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Put every binary in the same place so shared libraries are found at runtime
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# MinGW: use the LLD linker when installed. GNU ld crashes when linking
# the Vulkan code on some MinGW setups; LLD is compatible and faster.
# Executables are linked statically, so they don't need the MinGW runtime
# DLLs (libgcc, libwinpthread, and libstdc++ for the C++ example) to run.
if(MINGW)
    add_link_options(-static)

    include(CheckLinkerFlag)
    check_linker_flag(C "-fuse-ld=lld" ARCHTYPE_HAS_LLD)
    if(ARCHTYPE_HAS_LLD)
        add_link_options(-fuse-ld=lld)
        message(STATUS "Archtype: using the LLD linker")
    else()
        message(WARNING "Archtype: LLD not found. Install it with: "
                        "pacman -S mingw-w64-ucrt-x86_64-lld")
    endif()
endif()

# Project CMake helpers (cmake/ folder)
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(ArchtypeShaders)
include(ArchtypeAssets)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
include(FetchContent)

# Use the extraction time as file timestamps (avoids a CMake warning)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# SDL3: windows, events, time, executable path, Vulkan loading and surface
# creation. Built as a static library, so games don't need an extra SDL3.dll.
set(SDL_SHARED       OFF CACHE BOOL "" FORCE)
set(SDL_STATIC       ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS        OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL      OFF CACHE BOOL "" FORCE)

FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.18.zip
)
FetchContent_MakeAvailable(SDL3)

# SDL3_image: decodes image files (PNG, JPG, BMP, TGA, GIF, QOI...) for
# textures. PNG and JPG use its built-in decoders (the stb backend), so no
# libpng or libjpeg is needed. Formats that need extra libraries (AVIF, JXL,
# TIFF, WebP) are turned off: their sources are git submodules, which the
# release archive does not include.
set(SDLIMAGE_VENDORED    OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_DEPS_SHARED OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_BACKEND_STB ON  CACHE BOOL "" FORCE)
set(SDLIMAGE_PNG_LIBPNG  OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_AVIF        OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_JXL         OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TIF         OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_WEBP        OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_SAMPLES     OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TESTS       OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_INSTALL     OFF CACHE BOOL "" FORCE)

FetchContent_Declare(SDL3_image
    URL https://github.com/libsdl-org/SDL_image/archive/refs/tags/release-3.4.6.zip
)

# SDL3_image follows BUILD_SHARED_LIBS: force a static library, like SDL3,
# then restore the project's setting.
set(ARCHTYPE_SAVED_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
FetchContent_MakeAvailable(SDL3_image)
set(BUILD_SHARED_LIBS ${ARCHTYPE_SAVED_BUILD_SHARED_LIBS})

# Vulkan SDK: headers, validation layers and the glslc shader compiler
# (found through the VULKAN_SDK environment variable)
find_package(Vulkan REQUIRED COMPONENTS glslc)

# volk: takes every Vulkan function from the loader that SDL opens, so there
# is no Vulkan library to link against
set(VOLK_PULL_IN_VULKAN ON CACHE BOOL "" FORCE)
set(VOLK_INSTALL       OFF CACHE BOOL "" FORCE)

FetchContent_Declare(volk
    URL https://github.com/zeux/volk/archive/refs/tags/vulkan-sdk-1.4.363.0.zip
)
FetchContent_MakeAvailable(volk)

# ---------------------------------------------------------------------------
# Engine library
# ---------------------------------------------------------------------------
add_library(archtype
    src/core/file.c
    src/core/version.c
    src/platform/platform.c
    src/platform/window.c
    src/renderer/buffer.c
    src/renderer/camera2d.c
    src/renderer/camera3d.c
    src/renderer/fly_camera.c
    src/renderer/font.c
    src/renderer/image_loader.c
    src/renderer/pipeline.c
    src/renderer/renderer.c
    src/renderer/renderer2d.c
    src/renderer/shader.c
    src/renderer/texture.c
    src/renderer/texture_atlas.c
    src/renderer/vulkan/vk_allocator.c
    src/renderer/vulkan/vk_buffer.c
    src/renderer/vulkan/vk_common.c
    src/renderer/vulkan/vk_context.c
    src/renderer/vulkan/vk_descriptors.c
    src/renderer/vulkan/vk_device.c
    src/renderer/vulkan/vk_frame.c
    src/renderer/vulkan/vk_image.c
    src/renderer/vulkan/vk_swapchain.c
    src/renderer/vulkan/vk_upload.c
)
add_library(Archtype::archtype ALIAS archtype)

# Engine shaders (2D renderer...), compiled into the library as C arrays
archtype_embed_shaders(archtype
    src/renderer/shaders/renderer2d.vert
    src/renderer/shaders/renderer2d.frag
)

target_include_directories(archtype
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(archtype
    PRIVATE
        SDL3_image::SDL3_image-static
        SDL3::SDL3-static
        volk::volk
)

# The math library uses <math.h>, which needs libm on Linux
if(UNIX AND NOT APPLE)
    target_link_libraries(archtype PUBLIC m)
endif()

target_compile_definitions(archtype
    PRIVATE
        AT_BUILDING
        AT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        AT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        AT_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(archtype PUBLIC AT_SHARED)
    set_target_properties(archtype PROPERTIES
        C_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
endif()

if(MSVC)
    target_compile_options(archtype PRIVATE /W4)
else()
    target_compile_options(archtype PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
if(ARCHTYPE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(ARCHTYPE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()