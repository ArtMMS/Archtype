#ifndef ARCHTYPE_RENDERER_INTERNAL_H
#define ARCHTYPE_RENDERER_INTERNAL_H

/* Renderer internals shared by the files in src/renderer/. */
/* Not part of the public API.                              */

#include "archtype/renderer/renderer.h"
#include "archtype/renderer/buffer.h"
#include "archtype/renderer/pipeline.h"
#include "archtype/renderer/texture.h"

#include "renderer/vulkan/vk_allocator.h"
#include "renderer/vulkan/vk_buffer.h"
#include "renderer/vulkan/vk_context.h"
#include "renderer/vulkan/vk_descriptors.h"
#include "renderer/vulkan/vk_frame.h"
#include "renderer/vulkan/vk_image.h"
#include "renderer/vulkan/vk_swapchain.h"
#include "renderer/vulkan/vk_upload.h"

struct at_renderer {
    at_window         *window;
    bool               vsync;
    float              clear_color[4]; /* sRGB, as given by the user */

    /* Per-frame state, reset by begin_frame */
    bool               frame_started;
    at_pipeline       *bound_pipeline;
    bool               index_buffer_bound;
    at_buffer         *bound_buffers[AT_MAX_BINDINGS];  /* Uniform buffers by binding */
    at_texture        *bound_textures[AT_MAX_BINDINGS]; /* Textures by binding        */
    bool               descriptors_dirty; /* Bindings changed since the last draw */

    /* Window size used to build the current swapchain, to detect resizes */
    uint32_t           swapchain_width;
    uint32_t           swapchain_height;

    at_vk_context      context;
    VmaAllocator       allocator;
    at_vk_upload       upload;
    at_vk_swapchain    swapchain;
    at_vk_frames       frames;
    at_vk_descriptors  descriptors;
};

struct at_shader {
    VkShaderModule module;
};

struct at_pipeline {
    VkPipeline            handle;
    VkPipelineLayout      layout;
    VkDescriptorSetLayout set_layout;          /* VK_NULL_HANDLE if no bindings */
    uint32_t              binding_count;
    at_binding_desc       bindings[AT_MAX_BINDINGS];
    uint32_t              push_constant_size;
};

struct at_buffer {
    at_vk_buffer   buffer;
    at_buffer_type type;
    at_index_type  index_type;
    size_t         size;         /* Size requested by the user                   */
    VkDeviceSize   frame_stride; /* Uniform buffers: distance between the copies */
};

struct at_texture {
    at_vk_image image;
    VkSampler   sampler;
};

/* Command buffer being recorded for the current frame. */
static inline VkCommandBuffer at_renderer_command_buffer(const at_renderer *renderer) {
    return renderer->frames.frames[renderer->frames.current].command_buffer;
}

/* Frame in flight being recorded (0 .. AT_VK_FRAMES_IN_FLIGHT - 1). */
static inline uint32_t at_renderer_frame_index(const at_renderer *renderer) {
    return renderer->frames.current;
}

#endif /* ARCHTYPE_RENDERER_INTERNAL_H */