#ifndef ARCHTYPE_PLATFORM_INTERNAL_H
#define ARCHTYPE_PLATFORM_INTERNAL_H

/* Engine-only platform functions. Not part of the public API. */

#include "archtype/core/defines.h"

/* Returns true if the platform can create Vulkan surfaces. */
bool at_platform_vulkan_supported(void);

/* Returns the Vulkan instance extensions needed to draw into windows. */
/* The array is owned by the platform layer; do not free it.          */
const char **at_platform_get_vulkan_extensions(uint32_t *count);

#endif /* ARCHTYPE_PLATFORM_INTERNAL_H */