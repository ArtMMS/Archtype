cmake_minimum_required(VERSION 3.20)

project(Archtype
    VERSION 0.1.0
    DESCRIPTION "A 2D/3D game engine written in C with Vulkan"
    LANGUAGES C
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
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Default to Debug for single-config generators (Makefiles, Ninja)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Put every binary in the same place so shared libraries are found at runtime
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
include(FetchContent)

# Use the extraction time as file timestamps (avoids a CMake warning)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# GLFW: window creation, events and Vulkan surface creation
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
    URL https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.zip
)
FetchContent_MakeAvailable(glfw)

# ---------------------------------------------------------------------------
# Engine library
# ---------------------------------------------------------------------------
add_library(archtype
    src/core/version.c
    src/platform/platform.c
    src/platform/window.c
)
add_library(Archtype::archtype ALIAS archtype)

target_include_directories(archtype
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(archtype
    PRIVATE
        glfw
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
    enable_language(CXX)
    add_subdirectory(examples)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(ARCHTYPE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()