#ifndef ARCHTYPE_VK_DEVICE_H
#define ARCHTYPE_VK_DEVICE_H

/* GPU selection and logical device creation. */

#include "renderer/vulkan/vk_context.h"

/* Picks the best GPU that can draw into context->surface.  */
/* Fills physical_device, its properties and queue families. */
bool at_vk_device_select(at_vk_context *context);

/* Creates the logical device and gets its queues. */
bool at_vk_device_create(at_vk_context *context);

/* Destroys the logical device (waits for the GPU to finish first). */
void at_vk_device_destroy(at_vk_context *context);

#endif /* ARCHTYPE_VK_DEVICE_H */