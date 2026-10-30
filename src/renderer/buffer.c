#include "archtype/renderer/buffer.h"
#include "renderer/renderer_internal.h"

#include <stdlib.h>

at_buffer *at_buffer_create(at_renderer *renderer, const at_buffer_desc *desc) {
    if (!renderer || !desc || desc->size == 0) {
        fprintf(stderr, "[archtype] Invalid buffer description (size is required)\n");
        return NULL;
    }

    VkBufferUsageFlags usage = desc->type == AT_BUFFER_INDEX ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT
                                                             : VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

    at_buffer *buffer = (at_buffer *)calloc(1, sizeof(*buffer));
    if (!buffer) {
        fprintf(stderr, "[archtype] Failed to allocate buffer\n");
        return NULL;
    }

    buffer->type       = desc->type;
    buffer->index_type = desc->index_type;

    if (!at_vk_buffer_create_gpu(&buffer->buffer, renderer->allocator, desc->size, usage)) {
        free(buffer);
        return NULL;
    }

    if (desc->data &&
        !at_vk_buffer_upload(&buffer->buffer, renderer->allocator, &renderer->upload,
                             &renderer->context, desc->data, desc->size, 0)) {
        at_vk_buffer_destroy(&buffer->buffer, renderer->allocator);
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
    if (offset > buffer->buffer.size || size > buffer->buffer.size - offset) {
        fprintf(stderr, "[archtype] at_buffer_update: data does not fit in the buffer\n");
        return false;
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