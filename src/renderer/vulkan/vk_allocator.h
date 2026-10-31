#ifndef ARCHTYPE_VK_ALLOCATOR_H
#define ARCHTYPE_VK_ALLOCATOR_H

/*
 * GPU memory allocator. Not part of the public API.
 *
 * Vulkan limits how many VkDeviceMemory objects can exist at once (often
 * 4096), so resources don't get memory of their own. Memory is allocated in
 * big blocks instead, and each buffer or image gets a piece of one:
 *
 *   - One list of blocks per memory type, kept apart for buffers and for
 *     images. Linear resources (buffers) and optimal ones (images) never
 *     share a block, so bufferImageGranularity never applies.
 *   - The free space in a block is a list of ranges sorted by offset.
 *     Allocating takes the first range that fits (first-fit); freeing puts
 *     the range back and merges it with its neighbors.
 *   - Resources bigger than half a block get memory of their own.
 *   - CPU-visible memory is mapped once and stays mapped.
 *
 * Not thread-safe: use it from one thread only.
 */

#include "renderer/vulkan/vk_context.h"

#define AT_VK_POOL_COUNT 2 /* One for buffers, one for images */

/* Who reads and writes the memory. */
typedef enum at_vk_memory_usage {
    AT_VK_MEMORY_GPU = 0,   /* Fast GPU memory; the CPU can't access it          */
    AT_VK_MEMORY_CPU_TO_GPU /* Written by the CPU (mapped), read by the GPU      */
} at_vk_memory_usage;

struct at_vk_memory_block;

/* A piece of GPU memory bound to one buffer or image. */
typedef struct at_vk_allocation {
    struct at_vk_memory_block *block;       /* NULL for dedicated memory         */
    VkDeviceMemory             memory;
    VkDeviceSize               offset;      /* Where the piece starts in memory  */
    VkDeviceSize               size;
    uint32_t                   memory_type;
    void                      *mapped;      /* CPU pointer to the piece, or NULL */
} at_vk_allocation;

typedef struct at_vk_allocator {
    VkDevice                         device;
    VkPhysicalDeviceMemoryProperties memory_properties;
    VkDeviceSize                     non_coherent_atom_size;

    /* Block lists, by memory type and pool (buffers or images) */
    struct at_vk_memory_block       *pools[VK_MAX_MEMORY_TYPES][AT_VK_POOL_COUNT];

    uint32_t                         allocation_count; /* Live allocations           */
    uint32_t                         memory_count;     /* Live VkDeviceMemory objects */
} at_vk_allocator;

/* Prepares the allocator for the context's device. */
bool at_vk_allocator_create(at_vk_allocator *allocator, const at_vk_context *context);

/* Frees every block. Every buffer and image must be destroyed first. */
void at_vk_allocator_destroy(at_vk_allocator *allocator);

/* Allocates memory for a buffer and binds it. */
bool at_vk_allocator_alloc_buffer(at_vk_allocator   *allocator,
                                  VkBuffer           buffer,
                                  at_vk_memory_usage usage,
                                  at_vk_allocation  *allocation);

/* Allocates memory for an image and binds it. */
bool at_vk_allocator_alloc_image(at_vk_allocator   *allocator,
                                 VkImage            image,
                                 at_vk_memory_usage usage,
                                 at_vk_allocation  *allocation);

/* Gives the memory back (safe to call twice). The GPU must not be using it. */
void at_vk_allocator_free(at_vk_allocator *allocator, at_vk_allocation *allocation);

/* Makes CPU writes in [offset, offset + size) of the allocation visible to */
/* the GPU. size may be VK_WHOLE_SIZE. Does nothing on coherent memory.     */
void at_vk_allocator_flush(at_vk_allocator        *allocator,
                           const at_vk_allocation *allocation,
                           VkDeviceSize            offset,
                           VkDeviceSize            size);

#endif /* ARCHTYPE_VK_ALLOCATOR_H */