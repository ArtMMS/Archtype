#ifndef ARCHTYPE_VK_COMMON_H
#define ARCHTYPE_VK_COMMON_H

/* Shared Vulkan helpers for the engine. Not part of the public API. */

#include <stdio.h>

/* volk includes vulkan.h and loads every Vulkan function at runtime. */
/* Always include volk.h instead of vulkan/vulkan.h in the engine.     */
#include <volk.h>

#include "archtype/core/defines.h"

/* Returns the name of a VkResult, e.g. "VK_ERROR_OUT_OF_HOST_MEMORY". */
const char *at_vk_result_string(VkResult result);

/* Logs a failed Vulkan call. Use the result to decide what to do next. */
#define AT_VK_LOG_ERROR(call, result)                                       \
    fprintf(stderr, "[archtype] Vulkan: %s failed with %s (%s:%d)\n",       \
            call, at_vk_result_string(result), __FILE__, __LINE__)

#endif /* ARCHTYPE_VK_COMMON_H */