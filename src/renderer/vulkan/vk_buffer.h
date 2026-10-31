#ifndef ARCHTYPE_VK_BUFFER_H
#define ARCHTYPE_VK_BUFFER_H

/* GPU buffers, with memory from the engine's allocator. */
/* Not part of the public API.                           */

#include "renderer/vulkan/vk_allocator.h"
#include "renderer/vulkan/vk_upload.h"

typedef struct at_vk_buffer {
    VkBuffer         handle;
    at_vk_allocation allocation;
    VkDeviceSize     size;
    void            *mapped; /* CPU pointer for host-visible buffers, else NULL */
} at_vk_buffer;

/* Creates a buffer in fast GPU memory. Fill it with at_vk_buffer_upload. */
/* usage gets VK_BUFFER_USAGE_TRANSFER_DST_BIT added automatically.       */
bool at_vk_buffer_create_gpu(at_vk_buffer      *buffer,
                             at_vk_allocator   *allocator,
                             VkDeviceSize       size,
                             VkBufferUsageFlags usage);

/* Creates a buffer the CPU can write into directly (mapped). */
bool at_vk_buffer_create_host(at_vk_buffer      *buffer,
                              at_vk_allocator   *allocator,
                              VkDeviceSize       size,
                              VkBufferUsageFlags usage);

/* Destroys the buffer (safe to call twice). The GPU must not be using it. */
void at_vk_buffer_destroy(at_vk_buffer *buffer, at_vk_allocator *allocator);

/* Makes CPU writes to a host buffer in [offset, offset + size) visible to */
/* the GPU. size may be VK_WHOLE_SIZE.                                     */
void at_vk_buffer_flush(at_vk_buffer    *buffer,
                        at_vk_allocator *allocator,
                        VkDeviceSize     offset,
                        VkDeviceSize     size);

/* Copies data into a GPU buffer at offset through a temporary staging    */
/* buffer, and waits until the copy is done.                              */
bool at_vk_buffer_upload(at_vk_buffer        *buffer,
                         at_vk_allocator     *allocator,
                         at_vk_upload        *upload,
                         const at_vk_context *context,
                         const void          *data,
                         VkDeviceSize         size,
                         VkDeviceSize         offset);

#endif /* ARCHTYPE_VK_BUFFER_H */