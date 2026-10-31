cmake_minimum_required(VERSION 3.20)

project(Archtype
    VERSION 0.1.0
    DESCRIPTION "A 2D/3D game engine written in C with Vulkan"
    LANGUAGES C CXX # C++ is only needed to build VMA's implementation
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
# DLLs (libstdc++, libgcc, libwinpthread) to run.
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

# GLFW: window creation, events and Vulkan surface creation.
# Being replaced by SDL3 (ROADMAP 1.2.1).
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
    URL https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.zip
)
FetchContent_MakeAvailable(glfw)

# SDL3: windows, events, time, executable path and Vulkan surface creation.
# Built as a static library, so games don't need an extra SDL3.dll.
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

# Vulkan SDK: headers, validation layers and the glslc shader compiler
# (found through the VULKAN_SDK environment variable)
find_package(Vulkan REQUIRED COMPONENTS glslc)

# volk: loads Vulkan at runtime, so there is no Vulkan library to link against
set(VOLK_PULL_IN_VULKAN ON CACHE BOOL "" FORCE)
set(VOLK_INSTALL       OFF CACHE BOOL "" FORCE)

FetchContent_Declare(volk
    URL https://github.com/zeux/volk/archive/refs/tags/vulkan-sdk-1.4.363.0.zip
)
FetchContent_MakeAvailable(volk)

# VMA (Vulkan Memory Allocator): GPU memory for buffers and images
FetchContent_Declare(vma
    URL https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v3.3.0.zip
)
FetchContent_MakeAvailable(vma)

# stb_image: image loading for textures (single header, no CMake project).
# Temporary: will be replaced by SDL3_image (ROADMAP 1.2.1).
FetchContent_Declare(stb
    URL https://github.com/nothings/stb/archive/013ac3beddff3dbffafd5177e7972067cd2b5083.zip
)
FetchContent_MakeAvailable(stb)

# ---------------------------------------------------------------------------
# Engine library
# ---------------------------------------------------------------------------
add_library(archtype
    src/core/file.c
    src/core/version.c
    src/platform/platform.c
    src/platform/window.c
    src/renderer/buffer.c
    src/renderer/image_loader.c
    src/renderer/pipeline.c
    src/renderer/renderer.c
    src/renderer/shader.c
    src/renderer/stb_image_impl.c
    src/renderer/texture.c
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
    src/renderer/vulkan/vk_vma.cpp
)
add_library(Archtype::archtype ALIAS archtype)

# VMA's and stb_image's implementations are third-party code: don't show
# their warnings
set(ARCHTYPE_THIRD_PARTY_SOURCES
    src/renderer/vulkan/vk_vma.cpp
    src/renderer/stb_image_impl.c
)
if(MSVC)
    set_source_files_properties(${ARCHTYPE_THIRD_PARTY_SOURCES} PROPERTIES COMPILE_OPTIONS "/w")
else()
    set_source_files_properties(${ARCHTYPE_THIRD_PARTY_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
endif()

target_include_directories(archtype
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${stb_SOURCE_DIR}
)

target_link_libraries(archtype
    PRIVATE
        glfw
        SDL3::SDL3-static
        volk::volk
        GPUOpen::VulkanMemoryAllocator
)

# The math library uses <math.h>, which needs libm on Linux
if(UNIX AND NOT APPLE)
    target_link_libraries(archtype PUBLIC m)
endif()

target_compile_definitions(archtype
    PRIVATE
        AT_BUILDING
        GLFW_INCLUDE_NONE   # Do not let GLFW include OpenGL headers
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