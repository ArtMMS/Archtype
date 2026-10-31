#include "archtype/renderer/buffer.h"
#include "renderer/renderer_internal.h"

#include <stdlib.h>
#include <string.h>

static VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment) {
    return alignment > 0 ? (value + alignment - 1) / alignment * alignment : value;
}

/* Uniform buffers: AT_VK_FRAMES_IN_FLIGHT copies in CPU-visible memory.  */
/* Each frame writes and reads only its own copy, so writing never races  */
/* with the GPU reading a previous frame.                                 */
static bool create_uniform_buffer(at_renderer *renderer, at_buffer *buffer,
                                  const at_buffer_desc *desc) {
    VkDeviceSize alignment =
        renderer->context.physical_device_properties.limits.minUniformBufferOffsetAlignment;

    buffer->frame_stride = align_up(desc->size, alignment);

    if (!at_vk_buffer_create_host(&buffer->buffer, renderer->allocator,
                                  buffer->frame_stride * AT_VK_FRAMES_IN_FLIGHT,
                                  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)) {
        return false;
    }

    /* Start every copy with the initial data (or zeros) */
    for (uint32_t i = 0; i < AT_VK_FRAMES_IN_FLIGHT; ++i) {
        uint8_t *copy = (uint8_t *)buffer->buffer.mapped + buffer->frame_stride * i;
        if (desc->data) {
            memcpy(copy, desc->data, desc->size);
        } else {
            memset(copy, 0, desc->size);
        }
    }
    vmaFlushAllocation(renderer->allocator, buffer->buffer.allocation, 0, VK_WHOLE_SIZE);
    return true;
}

static bool create_gpu_buffer(at_renderer *renderer, at_buffer *buffer,
                              const at_buffer_desc *desc) {
    VkBufferUsageFlags usage = desc->type == AT_BUFFER_INDEX ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT
                                                             : VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

    if (!at_vk_buffer_create_gpu(&buffer->buffer, renderer->allocator, desc->size, usage)) {
        return false;
    }

    if (desc->data &&
        !at_vk_buffer_upload(&buffer->buffer, renderer->allocator, &renderer->upload,
                             &renderer->context, desc->data, desc->size, 0)) {
        at_vk_buffer_destroy(&buffer->buffer, renderer->allocator);
        return false;
    }
    return true;
}

at_buffer *at_buffer_create(at_renderer *renderer, const at_buffer_desc *desc) {
    if (!renderer || !desc || desc->size == 0) {
        fprintf(stderr, "[archtype] Invalid buffer description (size is required)\n");
        return NULL;
    }

    at_buffer *buffer = (at_buffer *)calloc(1, sizeof(*buffer));
    if (!buffer) {
        fprintf(stderr, "[archtype] Failed to allocate buffer\n");
        return NULL;
    }

    buffer->type       = desc->type;
    buffer->index_type = desc->index_type;
    buffer->size       = desc->size;

    bool ok = desc->type == AT_BUFFER_UNIFORM ? create_uniform_buffer(renderer, buffer, desc)
                                              : create_gpu_buffer(renderer, buffer, desc);
    if (!ok) {
        free(buffer);
        return NULL;
    }

    return buffer;
}

bool at_buffer_update(at_renderer *renderer, at_buffer *buffer,
                      const void *data, size_t size, size_t offset) {
    if (!renderer || !buffer || !data || size == 0) {
        return false;
    }
    if (offset > buffer->size || size > buffer->size - offset) {
        fprintf(stderr, "[archtype] at_buffer_update: data does not fit in the buffer\n");
        return false;
    }

    if (buffer->type == AT_BUFFER_UNIFORM) {
        /* Only after begin_frame: it waited until the GPU finished this */
        /* frame slot, so its copy is free to be written.                */
        if (!renderer->frame_started) {
            fprintf(stderr, "[archtype] Uniform buffers must be updated between "
                            "begin_frame and end_frame\n");
            return false;
        }

        VkDeviceSize frame_offset = buffer->frame_stride * at_renderer_frame_index(renderer);
        memcpy((uint8_t *)buffer->buffer.mapped + frame_offset + offset, data, size);
        vmaFlushAllocation(renderer->allocator, buffer->buffer.allocation,
                           frame_offset + offset, size);
        return true;
    }

    /* The GPU may be reading the buffer in a frame still in flight */
    vkDeviceWaitIdle(renderer->context.device);

    return at_vk_buffer_upload(&buffer->buffer, renderer->allocator, &renderer->upload,
                               &renderer->context, data, size, offset);
}

void at_buffer_destroy(at_renderer *renderer, at_buffer *buffer) {
    if (!renderer || !buffer) {
        return;
    }

    /* The GPU may still be reading this buffer */
    vkDeviceWaitIdle(renderer->context.device);

    at_vk_buffer_destroy(&buffer->buffer, renderer->allocator);
    free(buffer);
}