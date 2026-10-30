// VMA implementation.
//
// VMA is written in C++, so its implementation is compiled once, here.
// The rest of the engine is C and only uses VMA's C interface.

#include "renderer/vulkan/vk_allocator.h" // volk.h, then vk_mem_alloc.h

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>