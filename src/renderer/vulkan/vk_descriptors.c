#include "renderer/vulkan/vk_descriptors.h"

#include <stdlib.h>
#include <string.h>

/* Room in each pool. A frame that needs more gets another pool of the same */
/* size, so these only decide how often a new pool is added.               */
#define AT_VK_SETS_PER_POOL            1024
#define AT_VK_UNIFORM_BUFFERS_PER_POOL 1024
#define AT_VK_IMAGE_SAMPLERS_PER_POOL  1024

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

static bool create_pool(const at_vk_context *context, VkDescriptorPool *pool) {
    VkDescriptorPoolSize sizes[2];
    sizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sizes[0].descriptorCount = AT_VK_UNIFORM_BUFFERS_PER_POOL;
    sizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; /* Textures */
    sizes[1].descriptorCount = AT_VK_IMAGE_SAMPLERS_PER_POOL;

    VkDescriptorPoolCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.maxSets       = AT_VK_SETS_PER_POOL;
    info.poolSizeCount = 2;
    info.pPoolSizes    = sizes;

    VkResult result = vkCreateDescriptorPool(context->device, &info, NULL, pool);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkCreateDescriptorPool", result);
        *pool = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

/* Creates a pool and adds it to the end of the frame's list */
static bool add_pool(at_vk_descriptor_frame *frame, const at_vk_context *context) {
    if (frame->pool_count == frame->pool_capacity) {
        uint32_t          capacity = frame->pool_capacity ? frame->pool_capacity * 2 : 4;
        VkDescriptorPool *pools    = (VkDescriptorPool *)realloc(frame->pools,
                                                                 capacity * sizeof(*pools));
        if (!pools) {
            fprintf(stderr, "[archtype] Out of memory adding a descriptor pool\n");
            return false;
        }
        frame->pools         = pools;
        frame->pool_capacity = capacity;
    }

    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (!create_pool(context, &pool)) {
        return false;
    }

    frame->pools[frame->pool_count++] = pool;
    return true;
}

/* ------------------------------------------------------------------------ */
/* Public functions                                                          */
/* ------------------------------------------------------------------------ */

bool at_vk_descriptors_create(at_vk_descriptors *descriptors, const at_vk_context *context) {
    memset(descriptors, 0, sizeof(*descriptors));

    for (uint32_t i = 0; i < AT_VK_FRAMES_IN_FLIGHT; ++i) {
        if (!add_pool(&descriptors->frames[i], context)) {
            at_vk_descriptors_destroy(descriptors, context);
            return false;
        }
    }

    return true;
}

void at_vk_descriptors_destroy(at_vk_descriptors *descriptors, const at_vk_context *context) {
    for (uint32_t i = 0; i < AT_VK_FRAMES_IN_FLIGHT; ++i) {
        at_vk_descriptor_frame *frame = &descriptors->frames[i];

        for (uint32_t p = 0; p < frame->pool_count; ++p) {
            vkDestroyDescriptorPool(context->device, frame->pools[p], NULL);
        }
        free(frame->pools);
    }
    memset(descriptors, 0, sizeof(*descriptors));
}

void at_vk_descriptors_reset(at_vk_descriptors   *descriptors,
                             const at_vk_context *context,
                             uint32_t             frame) {
    at_vk_descriptor_frame *f = &descriptors->frames[frame];

    for (uint32_t p = 0; p < f->pool_count; ++p) {
        vkResetDescriptorPool(context->device, f->pools[p], 0);
    }
    f->current = 0;
}

VkDescriptorSet at_vk_descriptors_allocate(at_vk_descriptors    *descriptors,
                                           const at_vk_context  *context,
                                           uint32_t              frame,
                                           VkDescriptorSetLayout layout) {
    at_vk_descriptor_frame *f = &descriptors->frames[frame];

    VkDescriptorSetAllocateInfo info;
    memset(&info, 0, sizeof(info));
    info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    info.descriptorSetCount = 1;
    info.pSetLayouts        = &layout;

    for (;;) {
        /* Every pool of this frame is full: add one */
        bool new_pool = false;
        if (f->current >= f->pool_count) {
            if (!add_pool(f, context)) {
                return VK_NULL_HANDLE;
            }
            new_pool = true;
            fprintf(stderr, "[archtype] Descriptor pool added to frame %u (%u pools)\n",
                    frame, f->pool_count);
        }

        info.descriptorPool = f->pools[f->current];

        VkDescriptorSet set    = VK_NULL_HANDLE;
        VkResult        result = vkAllocateDescriptorSets(context->device, &info, &set);
        if (result == VK_SUCCESS) {
            return set;
        }

        if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL) {
            AT_VK_LOG_ERROR("vkAllocateDescriptorSets", result);
            return VK_NULL_HANDLE;
        }

        /* Even an empty pool can't hold it: more pools would not help */
        if (new_pool) {
            fprintf(stderr, "[archtype] A descriptor set layout needs more descriptors "
                            "than a pool holds\n");
            return VK_NULL_HANDLE;
        }

        /* This pool is full: try the next one */
        f->current++;
    }
}