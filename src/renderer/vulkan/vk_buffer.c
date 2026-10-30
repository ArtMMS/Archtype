#include "renderer/vulkan/vk_buffer.h"

#include <string.h>

static bool create_buffer(at_vk_buffer            *buffer,
                          VmaAllocator             allocator,
                          VkDeviceSize             size,
                          VkBufferUsageFlags       usage,
                          VmaAllocationCreateFlags flags) {
    memset(buffer, 0, sizeof(*buffer));

    VkBufferCreateInfo buffer_info;
    memset(&buffer_info, 0, sizeof(buffer_info));
    buffer_info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size        = size;
    buffer_info.usage       = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    /* VMA_MEMORY_USAGE_AUTO picks the memory type from usage and flags */
    VmaAllocationCreateInfo alloc_info;
    memset(&alloc_info, 0, sizeof(alloc_info));
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_info.flags = flags;

    VmaAllocationInfo allocation_info;
    VkResult result = vmaCreateBuffer(allocator, &buffer_info, &alloc_info,
                                      &buffer->handle, &buffer->allocation, &allocation_info);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vmaCreateBuffer", result);
        memset(buffer, 0, sizeof(*buffer));
        return false;
    }

    buffer->size   = size;
    buffer->mapped = allocation_info.pMappedData;
    return true;
}

bool at_vk_buffer_create_gpu(at_vk_buffer      *buffer,
                             VmaAllocator       allocator,
                             VkDeviceSize       size,
                             VkBufferUsageFlags usage) {
    return create_buffer(buffer, allocator, size,
                         usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0);
}

bool at_vk_buffer_create_host(at_vk_buffer      *buffer,
                              VmaAllocator       allocator,
                              VkDeviceSize       size,
                              VkBufferUsageFlags usage) {
    /* Sequential write: the CPU only writes (memcpy), never reads back */
    return create_buffer(buffer, allocator, size, usage,
                         VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                       | VMA_ALLOCATION_CREATE_MAPPED_BIT);
}

void at_vk_buffer_destroy(at_vk_buffer *buffer, VmaAllocator allocator) {
    if (buffer->handle != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, buffer->handle, buffer->allocation);
    }
    memset(buffer, 0, sizeof(*buffer));
}

bool at_vk_buffer_upload(at_vk_buffer        *buffer,
                         VmaAllocator         allocator,
                         at_vk_upload        *upload,
                         const at_vk_context *context,
                         const void          *data,
                         VkDeviceSize         size,
                         VkDeviceSize         offset) {
    /* 1. Put the data in memory the CPU can write to */
    at_vk_buffer staging;
    if (!at_vk_buffer_create_host(&staging, allocator, size,
                                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) {
        return false;
    }

    memcpy(staging.mapped, data, (size_t)size);
    vmaFlushAllocation(allocator, staging.allocation, 0, VK_WHOLE_SIZE);

    /* 2. Let the GPU copy it into fast GPU memory */
    VkCommandBuffer command_buffer = at_vk_upload_begin(upload, context);
    if (command_buffer == VK_NULL_HANDLE) {
        at_vk_buffer_destroy(&staging, allocator);
        return false;
    }

    VkBufferCopy region;
    region.srcOffset = 0;
    region.dstOffset = offset;
    region.size      = size;
    vkCmdCopyBuffer(command_buffer, staging.handle, buffer->handle, 1, &region);

    /* 3. Make the copied data visible to everything that reads it later */
    /*    (vertex input, index input, shaders...).                       */
    VkMemoryBarrier2 barrier;
    memset(&barrier, 0, sizeof(barrier));
    barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;

    VkDependencyInfo dependency;
    memset(&dependency, 0, sizeof(dependency));
    dependency.sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers    = &barrier;
    vkCmdPipelineBarrier2(command_buffer, &dependency);

    bool ok = at_vk_upload_submit(upload, context);

    /* The copy has finished (submit waits), so staging can go */
    at_vk_buffer_destroy(&staging, allocator);
    return ok;
}