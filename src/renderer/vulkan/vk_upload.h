#ifndef ARCHTYPE_VK_UPLOAD_H
#define ARCHTYPE_VK_UPLOAD_H

/* Immediate submits: record a few commands (e.g. copying data to the GPU), */
/* run them right away and wait until they finish. Used for uploads, not    */
/* for per-frame rendering. Not part of the public API.                     */

#include "renderer/vulkan/vk_context.h"

typedef struct at_vk_upload {
    VkCommandPool   command_pool;
    VkCommandBuffer command_buffer;
    VkFence         fence;
} at_vk_upload;

bool at_vk_upload_create(at_vk_upload *upload, const at_vk_context *context);

void at_vk_upload_destroy(at_vk_upload *upload, const at_vk_context *context);

/* Starts recording. Returns VK_NULL_HANDLE on failure. */
VkCommandBuffer at_vk_upload_begin(at_vk_upload *upload, const at_vk_context *context);

/* Submits the recorded commands and blocks until the GPU has run them. */
bool at_vk_upload_submit(at_vk_upload *upload, const at_vk_context *context);

#endif /* ARCHTYPE_VK_UPLOAD_H */