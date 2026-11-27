#ifndef ARCHTYPE_VK_DESCRIPTORS_H
#define ARCHTYPE_VK_DESCRIPTORS_H

/* Descriptor pools: each frame in flight has a list of pools, reset at the */
/* start of the frame, so descriptor sets are allocated freely and never    */
/* freed one by one. When a frame uses up its pools, another pool is added  */
/* to its list and kept for the next frames. Not part of the public API.    */

#include "renderer/vulkan/vk_context.h"
#include "renderer/vulkan/vk_frame.h"

/* The pools of one frame in flight. */
typedef struct at_vk_descriptor_frame {
    VkDescriptorPool *pools;
    uint32_t          pool_count;
    uint32_t          pool_capacity;
    uint32_t          current; /* Pool sets are being allocated from */
} at_vk_descriptor_frame;

typedef struct at_vk_descriptors {
    at_vk_descriptor_frame frames[AT_VK_FRAMES_IN_FLIGHT];
} at_vk_descriptors;

/* Creates one pool for every frame in flight. */
bool at_vk_descriptors_create(at_vk_descriptors *descriptors, const at_vk_context *context);

/* Destroys every pool (safe to call twice). */
void at_vk_descriptors_destroy(at_vk_descriptors *descriptors, const at_vk_context *context);

/* Frees every set allocated from this frame's pools. Call only after the */
/* GPU finished the previous use of the frame slot.                       */
void at_vk_descriptors_reset(at_vk_descriptors   *descriptors,
                             const at_vk_context *context,
                             uint32_t             frame);

/* Allocates one set with the given layout from this frame's pools, adding */
/* a pool when they are full. Returns VK_NULL_HANDLE on failure.           */
VkDescriptorSet at_vk_descriptors_allocate(at_vk_descriptors    *descriptors,
                                           const at_vk_context  *context,
                                           uint32_t              frame,
                                           VkDescriptorSetLayout layout);

#endif /* ARCHTYPE_VK_DESCRIPTORS_H */