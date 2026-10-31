#ifndef ARCHTYPE_VK_CONTEXT_H
#define ARCHTYPE_VK_CONTEXT_H

/* Core Vulkan objects shared by the whole renderer. */

#include "renderer/vulkan/vk_common.h"
#include "archtype/platform/window.h"

typedef struct at_vk_context_desc {
    at_window  *window;
    const char *app_name;
    bool        enable_validation;
} at_vk_context_desc;

typedef struct at_vk_context {
    /* Vulkan library, loaded through the platform layer */
    bool                     vulkan_loaded;

    /* Instance */
    VkInstance               instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    bool                     validation_enabled;

    /* Window surface */
    VkSurfaceKHR             surface;

    /* GPU */
    VkPhysicalDevice           physical_device;
    VkPhysicalDeviceProperties physical_device_properties;

    /* Logical device and queues */
    VkDevice                 device;
    VkQueue                  graphics_queue;
    VkQueue                  present_queue;
    uint32_t                 graphics_queue_family;
    uint32_t                 present_queue_family;
} at_vk_context;

/* Creates the instance, surface, GPU selection and logical device. */
/* On failure, everything already created is destroyed.             */
bool at_vk_context_init(at_vk_context *context, const at_vk_context_desc *desc);

/* Destroys everything created by at_vk_context_init (safe to call twice). */
void at_vk_context_shutdown(at_vk_context *context);

#endif /* ARCHTYPE_VK_CONTEXT_H */