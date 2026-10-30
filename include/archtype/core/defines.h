#ifndef ARCHTYPE_CORE_DEFINES_H
#define ARCHTYPE_CORE_DEFINES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------------ */
/* C++ compatibility                                                         */
/* ------------------------------------------------------------------------ */
#ifdef __cplusplus
#    define AT_EXTERN_C_BEGIN extern "C" {
#    define AT_EXTERN_C_END   }
#else
#    define AT_EXTERN_C_BEGIN
#    define AT_EXTERN_C_END
#endif

/* ------------------------------------------------------------------------ */
/* Platform detection                                                        */
/* ------------------------------------------------------------------------ */
#if defined(_WIN32)
#    define AT_PLATFORM_WINDOWS 1
#elif defined(__linux__)
#    define AT_PLATFORM_LINUX 1
#elif defined(__APPLE__)
#    define AT_PLATFORM_MACOS 1
#else
#    error "Archtype: unsupported platform"
#endif

/* ------------------------------------------------------------------------ */
/* Symbol export                                                             */
/* ------------------------------------------------------------------------ */
/* AT_SHARED is set by CMake when Archtype is built as a shared library.     */
/* AT_BUILDING is set only while compiling the engine itself.                */
#if defined(AT_SHARED)
#    if defined(AT_PLATFORM_WINDOWS)
#        if defined(AT_BUILDING)
#            define AT_API __declspec(dllexport)
#        else
#            define AT_API __declspec(dllimport)
#        endif
#    else
#        define AT_API __attribute__((visibility("default")))
#    endif
#else
#    define AT_API
#endif

#endif /* ARCHTYPE_CORE_DEFINES_H */