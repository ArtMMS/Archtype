#ifndef ARCHTYPE_VK_CONTEXT_H
#define ARCHTYPE_VK_CONTEXT_H

/* Core Vulkan objects shared by the whole renderer. */

#include "renderer/vulkan/vk_common.h"

typedef struct at_vk_context_desc {
    const char *app_name;
    bool        enable_validation;
} at_vk_context_desc;

typedef struct at_vk_context {
    VkInstance               instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    bool                     validation_enabled;
} at_vk_context;

/* Creates the Vulkan instance (and debug messenger when validating). */
bool at_vk_context_init(at_vk_context *context, const at_vk_context_desc *desc);

/* Destroys everything created by at_vk_context_init. */
void at_vk_context_shutdown(at_vk_context *context);

#endif /* ARCHTYPE_VK_CONTEXT_H */