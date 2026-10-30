/* readlink() is POSIX: request it before any system header is included */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L
#endif

#include "archtype/platform/platform.h"
#include "platform/platform_internal.h"

#include <stdio.h>
#include <string.h>

#if defined(AT_PLATFORM_WINDOWS)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#elif defined(AT_PLATFORM_LINUX)
#    include <unistd.h>
#elif defined(AT_PLATFORM_MACOS)
#    include <mach-o/dyld.h>
#endif

#include <GLFW/glfw3.h>

#define AT_MAX_PATH 1024

static bool s_initialized = false;
static char s_base_path[AT_MAX_PATH];
static bool s_base_path_ready = false;

static void glfw_error_callback(int code, const char *description) {
    fprintf(stderr, "[archtype] GLFW error %d: %s\n", code, description);
}

/* Writes the full path of the running executable into buffer. */
static bool get_executable_path(char *buffer, size_t size) {
#if defined(AT_PLATFORM_WINDOWS)
    DWORD length = GetModuleFileNameA(NULL, buffer, (DWORD)size);
    return length > 0 && length < size;
#elif defined(AT_PLATFORM_LINUX)
    ssize_t length = readlink("/proc/self/exe", buffer, size - 1);
    if (length <= 0) {
        return false;
    }
    buffer[length] = '\0';
    return true;
#elif defined(AT_PLATFORM_MACOS)
    uint32_t length = (uint32_t)size;
    return _NSGetExecutablePath(buffer, &length) == 0;
#else
    (void)buffer;
    (void)size;
    return false;
#endif
}

static bool is_absolute_path(const char *path) {
    if (path[0] == '/' || path[0] == '\\') {
        return true;
    }
    /* Windows drive letter, e.g. "D:/..." */
    return path[0] != '\0' && path[1] == ':';
}

bool at_platform_init(void) {
    if (s_initialized) {
        return true;
    }

    glfwSetErrorCallback(glfw_error_callback);

    if (!glfwInit()) {
        fprintf(stderr, "[archtype] Failed to initialize GLFW\n");
        return false;
    }

    s_initialized = true;
    return true;
}

void at_platform_shutdown(void) {
    if (!s_initialized) {
        return;
    }

    glfwTerminate();
    s_initialized = false;
}

void at_platform_poll_events(void) {
    glfwPollEvents();
}

void at_platform_wait_events(void) {
    glfwWaitEvents();
}

double at_platform_get_time(void) {
    return glfwGetTime();
}

const char *at_platform_get_base_path(void) {
    if (s_base_path_ready) {
        return s_base_path;
    }

    s_base_path[0] = '\0';
    if (get_executable_path(s_base_path, sizeof(s_base_path))) {
        /* Cut the executable name, keeping the last separator */
        char *slash     = strrchr(s_base_path, '/');
        char *backslash = strrchr(s_base_path, '\\');
        char *last      = slash;
        if (!last || (backslash && backslash > slash)) {
            last = backslash;
        }

        if (last) {
            last[1] = '\0';
        } else {
            s_base_path[0] = '\0';
        }
    }

    s_base_path_ready = true;
    return s_base_path;
}

bool at_platform_resolve_path(const char *path, char *out, size_t out_size) {
    int written = is_absolute_path(path)
                ? snprintf(out, out_size, "%s", path)
                : snprintf(out, out_size, "%s%s", at_platform_get_base_path(), path);

    return written >= 0 && (size_t)written < out_size;
}

bool at_platform_vulkan_supported(void) {
    return glfwVulkanSupported() == GLFW_TRUE;
}

const char **at_platform_get_vulkan_extensions(uint32_t *count) {
    return glfwGetRequiredInstanceExtensions(count);
}