#ifndef ARCHTYPE_VK_ALLOCATOR_H
#define ARCHTYPE_VK_ALLOCATOR_H

/* GPU memory allocator (VMA). Not part of the public API. */

#include "renderer/vulkan/vk_context.h"

/* VMA loads its Vulkan functions through volk (see vk_vma.cpp). */
/* volk.h is already included by vk_common.h, as VMA requires.   */
#define VMA_STATIC_VULKAN_FUNCTIONS  0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>

/* Creates the allocator for the context's device. */
bool at_vk_allocator_create(VmaAllocator *allocator, const at_vk_context *context);

/* Destroys the allocator. Every buffer and image must be destroyed first. */
void at_vk_allocator_destroy(VmaAllocator *allocator);

#endif /* ARCHTYPE_VK_ALLOCATOR_H */