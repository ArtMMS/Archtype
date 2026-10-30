#ifndef ARCHTYPE_PLATFORM_INTERNAL_H
#define ARCHTYPE_PLATFORM_INTERNAL_H

/* Engine-only platform functions. Not part of the public API. */

/* volk.h must come before GLFW so GLFW declares its Vulkan functions. */
#include <volk.h>

#include "archtype/core/defines.h"
#include "archtype/platform/window.h"

/* Returns true if the platform can create Vulkan surfaces. */
bool at_platform_vulkan_supported(void);

/* Returns the Vulkan instance extensions needed to draw into windows. */
/* The array is owned by the platform layer; do not free it.          */
const char **at_platform_get_vulkan_extensions(uint32_t *count);

/* Turns a relative path into one relative to the executable's folder.  */
/* Absolute paths are copied unchanged. Returns false if out is too     */
/* small.                                                               */
bool at_platform_resolve_path(const char *path, char *out, size_t out_size);

/* Creates a Vulkan surface for the window. */
VkResult at_window_create_vulkan_surface(const at_window *window,
                                         VkInstance       instance,
                                         VkSurfaceKHR    *surface);

#endif /* ARCHTYPE_PLATFORM_INTERNAL_H */