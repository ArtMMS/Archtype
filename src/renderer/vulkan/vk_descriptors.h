#ifndef ARCHTYPE_VK_DESCRIPTORS_H
#define ARCHTYPE_VK_DESCRIPTORS_H

/* Descriptor pools: one per frame in flight, reset at the start of the   */
/* frame, so descriptor sets are allocated freely and never freed one by  */
/* one. Not part of the public API.                                       */

#include "renderer/vulkan/vk_context.h"
#include "renderer/vulkan/vk_frame.h"

typedef struct at_vk_descriptors {
    VkDescriptorPool pools[AT_VK_FRAMES_IN_FLIGHT];
} at_vk_descriptors;

bool at_vk_descriptors_create(at_vk_descriptors *descriptors, const at_vk_context *context);

void at_vk_descriptors_destroy(at_vk_descriptors *descriptors, const at_vk_context *context);

/* Frees every set allocated from this frame's pool. Call only after the */
/* GPU finished the previous use of the frame slot.                      */
void at_vk_descriptors_reset(at_vk_descriptors   *descriptors,
                             const at_vk_context *context,
                             uint32_t             frame);

/* Allocates one set with the given layout from this frame's pool. */
/* Returns VK_NULL_HANDLE on failure.                               */
VkDescriptorSet at_vk_descriptors_allocate(at_vk_descriptors    *descriptors,
                                           const at_vk_context  *context,
                                           uint32_t              frame,
                                           VkDescriptorSetLayout layout);

#endif /* ARCHTYPE_VK_DESCRIPTORS_H */