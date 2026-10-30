#ifndef ARCHTYPE_RENDERER_INTERNAL_H
#define ARCHTYPE_RENDERER_INTERNAL_H

/* Renderer internals shared by the files in src/renderer/. */
/* Not part of the public API.                              */

#include "archtype/renderer/renderer.h"
#include "archtype/renderer/buffer.h"

#include "renderer/vulkan/vk_allocator.h"
#include "renderer/vulkan/vk_buffer.h"
#include "renderer/vulkan/vk_context.h"
#include "renderer/vulkan/vk_frame.h"
#include "renderer/vulkan/vk_swapchain.h"
#include "renderer/vulkan/vk_upload.h"

struct at_renderer {
    at_window       *window;
    bool             vsync;
    float            clear_color[4]; /* sRGB, as given by the user */

    /* Per-frame state */
    bool             frame_started;
    at_pipeline     *bound_pipeline;
    bool             index_buffer_bound;

    /* Window size used to build the current swapchain, to detect resizes */
    uint32_t         swapchain_width;
    uint32_t         swapchain_height;

    at_vk_context    context;
    VmaAllocator     allocator;
    at_vk_upload     upload;
    at_vk_swapchain  swapchain;
    at_vk_frames     frames;
};

struct at_shader {
    VkShaderModule module;
};

struct at_pipeline {
    VkPipeline       handle;
    VkPipelineLayout layout;
};

struct at_buffer {
    at_vk_buffer   buffer;
    at_buffer_type type;
    at_index_type  index_type;
};

/* Command buffer being recorded for the current frame. */
static inline VkCommandBuffer at_renderer_command_buffer(const at_renderer *renderer) {
    return renderer->frames.frames[renderer->frames.current].command_buffer;
}

#endif /* ARCHTYPE_RENDERER_INTERNAL_H */