#ifndef ARCHTYPE_VK_BUFFER_H
#define ARCHTYPE_VK_BUFFER_H

/* GPU buffers allocated with VMA. Not part of the public API. */

#include "renderer/vulkan/vk_allocator.h"
#include "renderer/vulkan/vk_upload.h"

typedef struct at_vk_buffer {
    VkBuffer      handle;
    VmaAllocation allocation;
    VkDeviceSize  size;
    void         *mapped; /* CPU pointer for host-visible buffers, else NULL */
} at_vk_buffer;

/* Creates a buffer in fast GPU memory. Fill it with at_vk_buffer_upload. */
/* usage gets VK_BUFFER_USAGE_TRANSFER_DST_BIT added automatically.       */
bool at_vk_buffer_create_gpu(at_vk_buffer      *buffer,
                             VmaAllocator       allocator,
                             VkDeviceSize       size,
                             VkBufferUsageFlags usage);

/* Creates a buffer the CPU can write into directly (mapped). */
bool at_vk_buffer_create_host(at_vk_buffer      *buffer,
                              VmaAllocator       allocator,
                              VkDeviceSize       size,
                              VkBufferUsageFlags usage);

/* Destroys the buffer (safe to call twice). The GPU must not be using it. */
void at_vk_buffer_destroy(at_vk_buffer *buffer, VmaAllocator allocator);

/* Copies data into a GPU buffer at offset through a temporary staging    */
/* buffer, and waits until the copy is done.                              */
bool at_vk_buffer_upload(at_vk_buffer        *buffer,
                         VmaAllocator         allocator,
                         at_vk_upload        *upload,
                         const at_vk_context *context,
                         const void          *data,
                         VkDeviceSize         size,
                         VkDeviceSize         offset);

#endif /* ARCHTYPE_VK_BUFFER_H */