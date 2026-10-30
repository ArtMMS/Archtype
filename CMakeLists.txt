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
# Engine library
# ---------------------------------------------------------------------------
add_library(archtype
    src/core/version.c
)
add_library(Archtype::archtype ALIAS archtype)

target_include_directories(archtype
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

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
    enable_language(CXX)
    add_subdirectory(examples)
endif()