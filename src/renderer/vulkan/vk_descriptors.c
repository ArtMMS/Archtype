#include "renderer/vulkan/vk_descriptors.h"

#include <string.h>

/* How many sets and descriptors each frame can use */
#define AT_VK_MAX_SETS_PER_FRAME     1024
#define AT_VK_MAX_UNIFORM_BUFFERS    1024
#define AT_VK_MAX_IMAGE_SAMPLERS     1024

bool at_vk_descriptors_create(at_vk_descriptors *descriptors, const at_vk_context *context) {
    memset(descriptors, 0, sizeof(*descriptors));

    VkDescriptorPoolSize sizes[2];
    sizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sizes[0].descriptorCount = AT_VK_MAX_UNIFORM_BUFFERS;
    sizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; /* Textures */
    sizes[1].descriptorCount = AT_VK_MAX_IMAGE_SAMPLERS;

    VkDescriptorPoolCreateInfo info;
    memset(&info, 0, sizeof(info));
    info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.maxSets       = AT_VK_MAX_SETS_PER_FRAME;
    info.poolSizeCount = 2;
    info.pPoolSizes    = sizes;

    for (uint32_t i = 0; i < AT_VK_FRAMES_IN_FLIGHT; ++i) {
        VkResult result = vkCreateDescriptorPool(context->device, &info, NULL,
                                                 &descriptors->pools[i]);
        if (result != VK_SUCCESS) {
            AT_VK_LOG_ERROR("vkCreateDescriptorPool", result);
            at_vk_descriptors_destroy(descriptors, context);
            return false;
        }
    }

    return true;
}

void at_vk_descriptors_destroy(at_vk_descriptors *descriptors, const at_vk_context *context) {
    for (uint32_t i = 0; i < AT_VK_FRAMES_IN_FLIGHT; ++i) {
        if (descriptors->pools[i] != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(context->device, descriptors->pools[i], NULL);
        }
    }
    memset(descriptors, 0, sizeof(*descriptors));
}

void at_vk_descriptors_reset(at_vk_descriptors   *descriptors,
                             const at_vk_context *context,
                             uint32_t             frame) {
    vkResetDescriptorPool(context->device, descriptors->pools[frame], 0);
}

VkDescriptorSet at_vk_descriptors_allocate(at_vk_descriptors    *descriptors,
                                           const at_vk_context  *context,
                                           uint32_t              frame,
                                           VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo info;
    memset(&info, 0, sizeof(info));
    info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    info.descriptorPool     = descriptors->pools[frame];
    info.descriptorSetCount = 1;
    info.pSetLayouts        = &layout;

    VkDescriptorSet set    = VK_NULL_HANDLE;
    VkResult        result = vkAllocateDescriptorSets(context->device, &info, &set);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkAllocateDescriptorSets", result);
        return VK_NULL_HANDLE;
    }
    return set;
}