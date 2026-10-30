#ifndef ARCHTYPE_RENDERER_INTERNAL_H
#define ARCHTYPE_RENDERER_INTERNAL_H

/* Renderer internals shared by renderer.c, shader.c and pipeline.c. */
/* Not part of the public API.                                       */

#include "archtype/renderer/renderer.h"

#include "renderer/vulkan/vk_context.h"
#include "renderer/vulkan/vk_frame.h"
#include "renderer/vulkan/vk_swapchain.h"

struct at_renderer {
    at_window       *window;
    bool             vsync;
    float            clear_color[4]; /* sRGB, as given by the user */
    bool             frame_started;
    at_pipeline     *bound_pipeline;

    /* Window size used to build the current swapchain, to detect resizes */
    uint32_t         swapchain_width;
    uint32_t         swapchain_height;

    at_vk_context    context;
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

/* Command buffer being recorded for the current frame. */
static inline VkCommandBuffer at_renderer_command_buffer(const at_renderer *renderer) {
    return renderer->frames.frames[renderer->frames.current].command_buffer;
}

#endif /* ARCHTYPE_RENDERER_INTERNAL_H */