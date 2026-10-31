#include "renderer/vulkan/vk_allocator.h"

#include <stdlib.h>
#include <string.h>

#define AT_VK_POOL_BUFFERS 0
#define AT_VK_POOL_IMAGES  1

#define AT_VK_MEGABYTE        ((VkDeviceSize)1024 * 1024)
#define AT_VK_BLOCK_SIZE      (64 * AT_VK_MEGABYTE)   /* Usual block size            */
#define AT_VK_SMALL_HEAP_SIZE (1024 * AT_VK_MEGABYTE) /* Heaps up to this use 1/8    */

/* A free range of a block. */
typedef struct at_vk_free_range {
    VkDeviceSize             offset;
    VkDeviceSize             size;
    struct at_vk_free_range *next;
} at_vk_free_range;

/* One VkDeviceMemory, shared by many resources. */
struct at_vk_memory_block {
    VkDeviceMemory             memory;
    VkDeviceSize               size;
    uint8_t                   *mapped;           /* NULL if not CPU-visible     */
    uint32_t                   memory_type;
    uint32_t                   pool;             /* AT_VK_POOL_BUFFERS / IMAGES */
    uint32_t                   allocation_count; /* Pieces in use               */
    at_vk_free_range          *free_list;        /* Sorted by offset            */
    struct at_vk_memory_block *next;
};

typedef struct at_vk_memory_block at_vk_memory_block;

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

static VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment) {
    return alignment > 0 ? (value + alignment - 1) / alignment * alignment : value;
}

static VkMemoryPropertyFlags memory_flags(const at_vk_allocator *allocator, uint32_t type) {
    return allocator->memory_properties.memoryTypes[type].propertyFlags;
}

/* Picks the memory type that best fits the usage among those the resource */
/* accepts (type_bits). Returns -1 if none fits.                            */
static int find_memory_type(const at_vk_allocator *allocator, uint32_t type_bits,
                            at_vk_memory_usage usage) {
    int best       = -1;
    int best_score = -1;

    for (uint32_t i = 0; i < allocator->memory_properties.memoryTypeCount; ++i) {
        if (!(type_bits & (1u << i))) {
            continue;
        }

        VkMemoryPropertyFlags flags = memory_flags(allocator, i);
        if (flags & (VK_MEMORY_PROPERTY_PROTECTED_BIT |
                     VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)) {
            continue; /* Special-purpose memory */
        }

        int score = 0;
        if (usage == AT_VK_MEMORY_CPU_TO_GPU) {
            if (!(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
                continue; /* The CPU must be able to write it */
            }
            if (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) {
                score += 2; /* No flushes needed */
            }
            if (!(flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) {
                score += 1; /* The CPU only writes: uncached memory is faster */
            }
        } else {
            if (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
                score += 2; /* Fast for the GPU */
            }
            if (!(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
                score += 1; /* Leave CPU-visible GPU memory to who needs it */
            }
        }

        if (score > best_score) {
            best       = (int)i;
            best_score = score;
        }
    }

    return best;
}

static VkDeviceSize block_size_for_type(const at_vk_allocator *allocator, uint32_t type) {
    uint32_t     heap      = allocator->memory_properties.memoryTypes[type].heapIndex;
    VkDeviceSize heap_size = allocator->memory_properties.memoryHeaps[heap].size;
    return heap_size <= AT_VK_SMALL_HEAP_SIZE ? heap_size / 8 : AT_VK_BLOCK_SIZE;
}

/* Allocates a VkDeviceMemory and maps all of it if the CPU can see it. */
static bool allocate_device_memory(at_vk_allocator *allocator, uint32_t type,
                                   VkDeviceSize size, VkDeviceMemory *memory, void **mapped) {
    VkMemoryAllocateInfo info;
    memset(&info, 0, sizeof(info));
    info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    info.allocationSize  = size;
    info.memoryTypeIndex = type;

    VkResult result = vkAllocateMemory(allocator->device, &info, NULL, memory);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkAllocateMemory", result);
        return false;
    }

    *mapped = NULL;
    if (memory_flags(allocator, type) & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        result = vkMapMemory(allocator->device, *memory, 0, VK_WHOLE_SIZE, 0, mapped);
        if (result != VK_SUCCESS) {
            AT_VK_LOG_ERROR("vkMapMemory", result);
            vkFreeMemory(allocator->device, *memory, NULL);
            *memory = VK_NULL_HANDLE;
            return false;
        }
    }

    allocator->memory_count++;
    return true;
}

/* Freeing also unmaps the memory. */
static void free_device_memory(at_vk_allocator *allocator, VkDeviceMemory memory) {
    vkFreeMemory(allocator->device, memory, NULL);
    allocator->memory_count--;
}

/* ------------------------------------------------------------------------ */
/* Blocks                                                                    */
/* ------------------------------------------------------------------------ */

static at_vk_memory_block *create_block(at_vk_allocator *allocator, uint32_t type,
                                        uint32_t pool, VkDeviceSize size) {
    at_vk_memory_block *block = (at_vk_memory_block *)calloc(1, sizeof(*block));
    at_vk_free_range   *range = (at_vk_free_range *)malloc(sizeof(*range));
    if (!block || !range) {
        fprintf(stderr, "[archtype] Out of memory creating a GPU memory block\n");
        free(block);
        free(range);
        return NULL;
    }

    void *mapped = NULL;
    if (!allocate_device_memory(allocator, type, size, &block->memory, &mapped)) {
        free(block);
        free(range);
        return NULL;
    }

    /* The whole block starts free */
    range->offset = 0;
    range->size   = size;
    range->next   = NULL;

    block->size        = size;
    block->mapped      = (uint8_t *)mapped;
    block->memory_type = type;
    block->pool        = pool;
    block->free_list   = range;

    fprintf(stderr, "[archtype] GPU memory block created: %u MB (%s, memory type %u)\n",
            (unsigned)(size / AT_VK_MEGABYTE),
            pool == AT_VK_POOL_IMAGES ? "images" : "buffers", type);
    return block;
}

static void destroy_block(at_vk_allocator *allocator, at_vk_memory_block *block) {
    at_vk_free_range *range = block->free_list;
    while (range) {
        at_vk_free_range *next = range->next;
        free(range);
        range = next;
    }

    free_device_memory(allocator, block->memory);
    free(block);
}

/* Finds the first free range where size bytes fit with the alignment and */
/* takes them. Returns false if the block has no room.                    */
static bool block_allocate(at_vk_memory_block *block, VkDeviceSize size,
                           VkDeviceSize alignment, VkDeviceSize *out_offset) {
    at_vk_free_range **link = &block->free_list;

    while (*link) {
        at_vk_free_range *range = *link;
        VkDeviceSize      start = align_up(range->offset, alignment);
        VkDeviceSize      end   = range->offset + range->size;

        if (start > end || end - start < size) {
            link = &range->next;
            continue;
        }

        VkDeviceSize front = start - range->offset;  /* Free space left before */
        VkDeviceSize back  = end - (start + size);   /* Free space left after  */

        if (front > 0 && back > 0) {
            /* The range splits in two: keep the front, add the back */
            at_vk_free_range *after = (at_vk_free_range *)malloc(sizeof(*after));
            if (!after) {
                return false;
            }
            after->offset = start + size;
            after->size   = back;
            after->next   = range->next;
            range->size   = front;
            range->next   = after;
        } else if (front > 0) {
            range->size = front;
        } else if (back > 0) {
            range->offset = start + size;
            range->size   = back;
        } else {
            *link = range->next; /* Used up completely */
            free(range);
        }

        block->allocation_count++;
        *out_offset = start;
        return true;
    }

    return false;
}

/* Puts [offset, offset + size) back in the free list, merging it with the */
/* free ranges right before and after it.                                  */
static void block_free(at_vk_memory_block *block, VkDeviceSize offset, VkDeviceSize size) {
    at_vk_free_range *prev = NULL;
    at_vk_free_range *next = block->free_list;
    while (next && next->offset < offset) {
        prev = next;
        next = next->next;
    }

    bool joins_prev = prev && prev->offset + prev->size == offset;
    bool joins_next = next && offset + size == next->offset;

    if (joins_prev && joins_next) {
        prev->size += size + next->size;
        prev->next  = next->next;
        free(next);
    } else if (joins_prev) {
        prev->size += size;
    } else if (joins_next) {
        next->offset  = offset;
        next->size   += size;
    } else {
        at_vk_free_range *range = (at_vk_free_range *)malloc(sizeof(*range));
        if (range) {
            range->offset = offset;
            range->size   = size;
            range->next   = next;
            if (prev) {
                prev->next = range;
            } else {
                block->free_list = range;
            }
        } else {
            /* The range stays unusable until the block is destroyed */
            fprintf(stderr, "[archtype] Out of memory freeing GPU memory\n");
        }
    }

    block->allocation_count--;
}

/* Destroys an empty block, unless it is the only one of its pool: keeping */
/* one avoids allocating device memory again for every short-lived buffer  */
/* (e.g. staging buffers).                                                 */
static void release_empty_block(at_vk_allocator *allocator, at_vk_memory_block *block) {
    at_vk_memory_block **link = &allocator->pools[block->memory_type][block->pool];
    if (*link == block && block->next == NULL) {
        return;
    }

    while (*link && *link != block) {
        link = &(*link)->next;
    }
    if (*link) {
        *link = block->next;
        destroy_block(allocator, block);
    }
}

/* ------------------------------------------------------------------------ */
/* Allocation                                                                */
/* ------------------------------------------------------------------------ */

static bool allocate(at_vk_allocator *allocator, const VkMemoryRequirements *requirements,
                     at_vk_memory_usage usage, uint32_t pool, at_vk_allocation *allocation) {
    memset(allocation, 0, sizeof(*allocation));

    int found = find_memory_type(allocator, requirements->memoryTypeBits, usage);
    if (found < 0) {
        fprintf(stderr, "[archtype] No suitable GPU memory type for this resource\n");
        return false;
    }

    uint32_t     type       = (uint32_t)found;
    VkDeviceSize block_size = block_size_for_type(allocator, type);

    /* Big resources: memory of their own */
    if (requirements->size > block_size / 2) {
        void *mapped = NULL;
        if (!allocate_device_memory(allocator, type, requirements->size,
                                    &allocation->memory, &mapped)) {
            return false;
        }

        allocation->size        = requirements->size;
        allocation->memory_type = type;
        allocation->mapped      = mapped;
        allocator->allocation_count++;
        return true;
    }

    /* Small resources: a piece of the first block with room */
    at_vk_memory_block **pool_head = &allocator->pools[type][pool];
    at_vk_memory_block  *block     = *pool_head;
    VkDeviceSize         offset    = 0;

    while (block && !block_allocate(block, requirements->size, requirements->alignment,
                                    &offset)) {
        block = block->next;
    }

    if (!block) {
        block = create_block(allocator, type, pool, block_size);
        if (!block) {
            return false;
        }
        block->next = *pool_head;
        *pool_head  = block;

        if (!block_allocate(block, requirements->size, requirements->alignment, &offset)) {
            fprintf(stderr, "[archtype] Out of memory allocating GPU memory\n");
            return false;
        }
    }

    allocation->block       = block;
    allocation->memory      = block->memory;
    allocation->offset      = offset;
    allocation->size        = requirements->size;
    allocation->memory_type = type;
    allocation->mapped      = block->mapped ? block->mapped + offset : NULL;
    allocator->allocation_count++;
    return true;
}

/* ------------------------------------------------------------------------ */
/* Public functions                                                          */
/* ------------------------------------------------------------------------ */

bool at_vk_allocator_create(at_vk_allocator *allocator, const at_vk_context *context) {
    memset(allocator, 0, sizeof(*allocator));

    allocator->device = context->device;
    vkGetPhysicalDeviceMemoryProperties(context->physical_device,
                                        &allocator->memory_properties);

    allocator->non_coherent_atom_size =
        context->physical_device_properties.limits.nonCoherentAtomSize;
    if (allocator->non_coherent_atom_size == 0) {
        allocator->non_coherent_atom_size = 1;
    }

    fprintf(stderr, "[archtype] GPU memory allocator created (%u memory types, %u heaps)\n",
            allocator->memory_properties.memoryTypeCount,
            allocator->memory_properties.memoryHeapCount);
    return true;
}

void at_vk_allocator_destroy(at_vk_allocator *allocator) {
    if (allocator->device == VK_NULL_HANDLE) {
        return;
    }

    if (allocator->allocation_count > 0) {
        fprintf(stderr, "[archtype] Warning: %u GPU allocations were not freed "
                        "(destroy every buffer and texture before the renderer)\n",
                allocator->allocation_count);
    }

    for (uint32_t type = 0; type < VK_MAX_MEMORY_TYPES; ++type) {
        for (uint32_t pool = 0; pool < AT_VK_POOL_COUNT; ++pool) {
            at_vk_memory_block *block = allocator->pools[type][pool];
            while (block) {
                at_vk_memory_block *next = block->next;
                destroy_block(allocator, block);
                block = next;
            }
        }
    }

    memset(allocator, 0, sizeof(*allocator));
}

bool at_vk_allocator_alloc_buffer(at_vk_allocator   *allocator,
                                  VkBuffer           buffer,
                                  at_vk_memory_usage usage,
                                  at_vk_allocation  *allocation) {
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(allocator->device, buffer, &requirements);

    if (!allocate(allocator, &requirements, usage, AT_VK_POOL_BUFFERS, allocation)) {
        return false;
    }

    VkResult result = vkBindBufferMemory(allocator->device, buffer,
                                         allocation->memory, allocation->offset);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkBindBufferMemory", result);
        at_vk_allocator_free(allocator, allocation);
        return false;
    }
    return true;
}

bool at_vk_allocator_alloc_image(at_vk_allocator   *allocator,
                                 VkImage            image,
                                 at_vk_memory_usage usage,
                                 at_vk_allocation  *allocation) {
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(allocator->device, image, &requirements);

    if (!allocate(allocator, &requirements, usage, AT_VK_POOL_IMAGES, allocation)) {
        return false;
    }

    VkResult result = vkBindImageMemory(allocator->device, image,
                                        allocation->memory, allocation->offset);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkBindImageMemory", result);
        at_vk_allocator_free(allocator, allocation);
        return false;
    }
    return true;
}

void at_vk_allocator_free(at_vk_allocator *allocator, at_vk_allocation *allocation) {
    if (allocation->memory == VK_NULL_HANDLE) {
        return;
    }

    at_vk_memory_block *block = allocation->block;
    if (!block) {
        free_device_memory(allocator, allocation->memory); /* Dedicated */
    } else {
        block_free(block, allocation->offset, allocation->size);
        if (block->allocation_count == 0) {
            release_empty_block(allocator, block);
        }
    }

    allocator->allocation_count--;
    memset(allocation, 0, sizeof(*allocation));
}

void at_vk_allocator_flush(at_vk_allocator        *allocator,
                           const at_vk_allocation *allocation,
                           VkDeviceSize            offset,
                           VkDeviceSize            size) {
    if (allocation->memory == VK_NULL_HANDLE || offset >= allocation->size) {
        return;
    }

    /* Coherent memory is seen by the GPU without flushing */
    VkMemoryPropertyFlags flags = memory_flags(allocator, allocation->memory_type);
    if (!(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) ||
        (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        return;
    }

    if (size == VK_WHOLE_SIZE || size > allocation->size - offset) {
        size = allocation->size - offset;
    }

    /* The flushed range must start and end on nonCoherentAtomSize, or reach */
    /* the end of the VkDeviceMemory.                                        */
    VkDeviceSize atom        = allocator->non_coherent_atom_size;
    VkDeviceSize memory_size = allocation->block ? allocation->block->size : allocation->size;
    VkDeviceSize start       = allocation->offset + offset;
    VkDeviceSize begin       = start / atom * atom;
    VkDeviceSize end         = align_up(start + size, atom);

    VkMappedMemoryRange range;
    memset(&range, 0, sizeof(range));
    range.sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = allocation->memory;
    range.offset = begin;
    range.size   = end >= memory_size ? VK_WHOLE_SIZE : end - begin;

    VkResult result = vkFlushMappedMemoryRanges(allocator->device, 1, &range);
    if (result != VK_SUCCESS) {
        AT_VK_LOG_ERROR("vkFlushMappedMemoryRanges", result);
    }
}