#include "renderer/renderer_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

/* sRGB -> linear. sRGB images expect linear values and convert them back */
/* to sRGB when writing, so the color on screen matches the given value.  */
static float srgb_to_linear(float c) {
    if (c <= 0.04045f) {
        return c / 12.92f;
    }
    return powf((c + 0.055f) / 1.055f, 2.4f);
}

static bool is_srgb_format(VkFormat format) {
    return format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_R8G8B8A8_SRGB;
}

static bool recreate_swapchain(at_renderer *renderer, uint32_t width, uint32_t height) {
    /* The GPU may still be using the old images: wait until it is done */
    vkDeviceWaitIdle(renderer->context.device);

    renderer->frames.out_of_date = false;
    renderer->swapchain_width    = width;
    renderer->swapchain_height   = height;

    return at_vk_swapchain_recreate(&renderer->swapchain, &renderer->context,
                                    width, height, renderer->vsync);
}

/* Viewport and scissor are dynamic: they cover the whole window every frame, */
/* so pipelines don't need to be recreated when the window is resized.        */
static void set_viewport_and_scissor(at_renderer *renderer) {
    VkCommandBuffer command_buffer = at_renderer_command_buffer(renderer);
    VkExtent2D      extent         = renderer->swapchain.extent;

    VkViewport viewport;
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = (float)extent.width;
    viewport.height   = (float)extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);

    VkRect2D scissor;
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent   = extent;
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);
}

static bool check_recording(const at_renderer *renderer, const char *function) {
    if (!renderer->frame_started) {
        fprintf(stderr, "[archtype] %s must be called between begin_frame and end_frame\n",
                function);
        return false;
    }
    return true;
}

static void reset_frame_state(at_renderer *renderer) {
    renderer->bound_pipeline     = NULL;
    renderer->index_buffer_bound = false;
    renderer->descriptors_dirty  = true;
    memset(renderer->bound_buffers, 0, sizeof(renderer->bound_buffers));
    memset(renderer->bound_textures, 0, sizeof(renderer->bound_textures));
}

/* Before a draw: if the bound pipeline reads resources and the bindings  */
/* changed, allocate a descriptor set from this frame's pool, point it at */
/* the bound resources and bind it.                                       */
static bool prepare_descriptors(at_renderer *renderer) {
    at_pipeline *pipeline = renderer->bound_pipeline;
    if (pipeline->set_layout == VK_NULL_HANDLE || !renderer->descriptors_dirty) {
        return true;
    }

    uint32_t        frame = at_renderer_frame_index(renderer);
    VkDescriptorSet set   = at_vk_descriptors_allocate(&renderer->descriptors,
                                                       &renderer->context, frame,
                                                       pipeline->set_layout);
    if (set == VK_NULL_HANDLE) {
        return false;
    }

    VkDescriptorBufferInfo buffer_infos[AT_MAX_BINDINGS];
    VkDescriptorImageInfo  image_infos[AT_MAX_BINDINGS];
    VkWriteDescriptorSet   writes[AT_MAX_BINDINGS];
    memset(writes, 0, sizeof(writes));

    for (uint32_t i = 0; i < pipeline->binding_count; ++i) {
        const at_binding_desc *binding = &pipeline->bindings[i];

        writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet          = set;
        writes[i].dstBinding      = binding->binding;
        writes[i].descriptorCount = 1;

        if (binding->type == AT_BINDING_TEXTURE) {
            at_texture *texture = renderer->bound_textures[binding->binding];
            if (!texture) {
                fprintf(stderr, "[archtype] Draw skipped: no texture bound to binding %u\n",
                        binding->binding);
                return false;
            }

            image_infos[i].sampler     = texture->sampler;
            image_infos[i].imageView   = texture->image.view;
            image_infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo     = &image_infos[i];
        } else {
            at_buffer *buffer = renderer->bound_buffers[binding->binding];
            if (!buffer) {
                fprintf(stderr, "[archtype] Draw skipped: no uniform buffer bound to "
                                "binding %u\n", binding->binding);
                return false;
            }

            /* Point at this frame's copy of the uniform buffer */
            buffer_infos[i].buffer = buffer->buffer.handle;
            buffer_infos[i].offset = buffer->frame_stride * frame;
            buffer_infos[i].range  = buffer->size;

            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[i].pBufferInfo    = &buffer_infos[i];
        }
    }

    vkUpdateDescriptorSets(renderer->context.device, pipeline->binding_count, writes, 0, NULL);
    vkCmdBindDescriptorSets(at_renderer_command_buffer(renderer),
                            VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->layout,
                            0, 1, &set, 0, NULL);

    renderer->descriptors_dirty = false;
    return true;
}

/* Shared checks for every draw call */
static bool prepare_draw(at_renderer *renderer, const char *function) {
    if (!check_recording(renderer, function)) {
        return false;
    }
    if (!renderer->bound_pipeline) {
        fprintf(stderr, "[archtype] %s called without a bound pipeline\n", function);
        return false;
    }
    return prepare_descriptors(renderer);
}

/* ------------------------------------------------------------------------ */
/* Creation and destruction                                                  */
/* ------------------------------------------------------------------------ */

at_renderer *at_renderer_create(const at_renderer_desc *desc) {
    if (!desc || !desc->window) {
        fprintf(stderr, "[archtype] Invalid renderer description\n");
        return NULL;
    }

    at_renderer *renderer = (at_renderer *)calloc(1, sizeof(*renderer));
    if (!renderer) {
        fprintf(stderr, "[archtype] Failed to allocate renderer\n");
        return NULL;
    }

    renderer->window         = desc->window;
    renderer->vsync          = desc->vsync;
    renderer->clear_color[3] = 1.0f; /* Opaque black */

    at_vk_context_desc context_desc;
    context_desc.window            = desc->window;
    context_desc.app_name          = desc->app_name;
    context_desc.enable_validation = desc->enable_validation;

    if (!at_vk_context_init(&renderer->context, &context_desc)) {
        free(renderer);
        return NULL;
    }

    int32_t width  = 0;
    int32_t height = 0;
    at_window_get_size(desc->window, &width, &height);

    renderer->swapchain_width  = (uint32_t)width;
    renderer->swapchain_height = (uint32_t)height;

    bool ok = at_vk_allocator_create(&renderer->allocator, &renderer->context)
           && at_vk_upload_create(&renderer->upload, &renderer->context)
           && at_vk_swapchain_create(&renderer->swapchain, &renderer->context,
                                     renderer->swapchain_width, renderer->swapchain_height,
                                     renderer->vsync)
           && at_vk_frames_create(&renderer->frames, &renderer->context)
           && at_vk_descriptors_create(&renderer->descriptors, &renderer->context);

    if (!ok) {
        at_renderer_destroy(renderer);
        return NULL;
    }

    return renderer;
}

void at_renderer_destroy(at_renderer *renderer) {
    if (!renderer) {
        return;
    }

    /* Wait for the GPU before destroying anything it may still be using. */
    /* Destroy in reverse order of creation.                              */
    if (renderer->context.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(renderer->context.device);
        at_vk_descriptors_destroy(&renderer->descriptors, &renderer->context);
        at_vk_frames_destroy(&renderer->frames, &renderer->context);
        at_vk_swapchain_destroy(&renderer->swapchain, &renderer->context);
        at_vk_upload_destroy(&renderer->upload, &renderer->context);
        at_vk_allocator_destroy(&renderer->allocator);
    }

    at_vk_context_shutdown(&renderer->context);
    free(renderer);
}

void at_renderer_get_size(const at_renderer *renderer, uint32_t *width, uint32_t *height) {
    if (width) {
        *width = renderer->swapchain.extent.width;
    }
    if (height) {
        *height = renderer->swapchain.extent.height;
    }
}

/* ------------------------------------------------------------------------ */
/* Frame                                                                     */
/* ------------------------------------------------------------------------ */

void at_renderer_set_clear_color(at_renderer *renderer, float r, float g, float b, float a) {
    renderer->clear_color[0] = r;
    renderer->clear_color[1] = g;
    renderer->clear_color[2] = b;
    renderer->clear_color[3] = a;
}

bool at_renderer_begin_frame(at_renderer *renderer) {
    if (renderer->frame_started) {
        fprintf(stderr, "[archtype] at_renderer_begin_frame called twice without end_frame\n");
        return false;
    }

    int32_t width  = 0;
    int32_t height = 0;
    at_window_get_size(renderer->window, &width, &height);

    /* Minimized: there is nothing to draw into */
    if (width <= 0 || height <= 0) {
        return false;
    }

    /* Recreate the swapchain if the window was resized, Vulkan reported it */
    /* as out of date, or a previous recreation failed.                    */
    bool size_changed = (uint32_t)width  != renderer->swapchain_width
                     || (uint32_t)height != renderer->swapchain_height;

    if (size_changed || renderer->frames.out_of_date ||
        renderer->swapchain.handle == VK_NULL_HANDLE) {
        if (!recreate_swapchain(renderer, (uint32_t)width, (uint32_t)height)) {
            return false;
        }
    }

    float clear[4];
    for (int i = 0; i < 3; ++i) {
        clear[i] = is_srgb_format(renderer->swapchain.format)
                 ? srgb_to_linear(renderer->clear_color[i])
                 : renderer->clear_color[i];
    }
    clear[3] = renderer->clear_color[3]; /* Alpha is always linear */

    /* If this fails because the swapchain went out of date, it is */
    /* recreated at the start of the next frame.                   */
    if (!at_vk_frame_begin(&renderer->frames, &renderer->context,
                           &renderer->swapchain, clear)) {
        return false;
    }

    /* The frame slot is free now (begin waited for it): recycle its sets */
    at_vk_descriptors_reset(&renderer->descriptors, &renderer->context,
                            at_renderer_frame_index(renderer));

    renderer->frame_started = true;
    reset_frame_state(renderer);
    set_viewport_and_scissor(renderer);
    return true;
}

void at_renderer_end_frame(at_renderer *renderer) {
    if (!renderer->frame_started) {
        fprintf(stderr, "[archtype] at_renderer_end_frame called without begin_frame\n");
        return;
    }

    at_vk_frame_end(&renderer->frames, &renderer->context, &renderer->swapchain);
    renderer->frame_started = false;
    reset_frame_state(renderer);
}

/* ------------------------------------------------------------------------ */
/* Draw commands                                                             */
/* ------------------------------------------------------------------------ */

void at_renderer_bind_pipeline(at_renderer *renderer, at_pipeline *pipeline) {
    if (!check_recording(renderer, "at_renderer_bind_pipeline") || !pipeline) {
        return;
    }

    vkCmdBindPipeline(at_renderer_command_buffer(renderer), VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline->handle);
    renderer->bound_pipeline    = pipeline;
    renderer->descriptors_dirty = true;
}

void at_renderer_bind_vertex_buffer(at_renderer *renderer, at_buffer *buffer) {
    if (!check_recording(renderer, "at_renderer_bind_vertex_buffer") || !buffer) {
        return;
    }
    if (buffer->type != AT_BUFFER_VERTEX) {
        fprintf(stderr, "[archtype] at_renderer_bind_vertex_buffer: not a vertex buffer\n");
        return;
    }

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(at_renderer_command_buffer(renderer), 0, 1,
                           &buffer->buffer.handle, &offset);
}

void at_renderer_bind_index_buffer(at_renderer *renderer, at_buffer *buffer) {
    if (!check_recording(renderer, "at_renderer_bind_index_buffer") || !buffer) {
        return;
    }
    if (buffer->type != AT_BUFFER_INDEX) {
        fprintf(stderr, "[archtype] at_renderer_bind_index_buffer: not an index buffer\n");
        return;
    }

    VkIndexType index_type = buffer->index_type == AT_INDEX_UINT32 ? VK_INDEX_TYPE_UINT32
                                                                   : VK_INDEX_TYPE_UINT16;
    vkCmdBindIndexBuffer(at_renderer_command_buffer(renderer), buffer->buffer.handle, 0,
                         index_type);
    renderer->index_buffer_bound = true;
}

void at_renderer_bind_uniform_buffer(at_renderer *renderer, uint32_t binding,
                                     at_buffer *buffer) {
    if (!check_recording(renderer, "at_renderer_bind_uniform_buffer") || !buffer) {
        return;
    }
    if (buffer->type != AT_BUFFER_UNIFORM) {
        fprintf(stderr, "[archtype] at_renderer_bind_uniform_buffer: not a uniform buffer\n");
        return;
    }
    if (binding >= AT_MAX_BINDINGS) {
        fprintf(stderr, "[archtype] at_renderer_bind_uniform_buffer: binding %u out of "
                        "range\n", binding);
        return;
    }

    renderer->bound_buffers[binding] = buffer;
    renderer->descriptors_dirty      = true;
}

void at_renderer_bind_texture(at_renderer *renderer, uint32_t binding, at_texture *texture) {
    if (!check_recording(renderer, "at_renderer_bind_texture") || !texture) {
        return;
    }
    if (binding >= AT_MAX_BINDINGS) {
        fprintf(stderr, "[archtype] at_renderer_bind_texture: binding %u out of range\n",
                binding);
        return;
    }

    renderer->bound_textures[binding] = texture;
    renderer->descriptors_dirty       = true;
}

void at_renderer_push_constants(at_renderer *renderer, const void *data, uint32_t size) {
    if (!check_recording(renderer, "at_renderer_push_constants") || !data) {
        return;
    }

    at_pipeline *pipeline = renderer->bound_pipeline;
    if (!pipeline) {
        fprintf(stderr, "[archtype] at_renderer_push_constants needs a bound pipeline\n");
        return;
    }
    if (size == 0 || size > pipeline->push_constant_size || size % 4 != 0) {
        fprintf(stderr, "[archtype] at_renderer_push_constants: size %u does not fit the "
                        "pipeline's push_constant_size (%u)\n",
                size, pipeline->push_constant_size);
        return;
    }

    vkCmdPushConstants(at_renderer_command_buffer(renderer), pipeline->layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, size, data);
}

void at_renderer_draw(at_renderer *renderer, uint32_t vertex_count) {
    if (!prepare_draw(renderer, "at_renderer_draw")) {
        return;
    }

    vkCmdDraw(at_renderer_command_buffer(renderer), vertex_count, 1, 0, 0);
}

void at_renderer_draw_indexed(at_renderer *renderer, uint32_t index_count) {
    if (renderer->frame_started && !renderer->index_buffer_bound) {
        fprintf(stderr, "[archtype] at_renderer_draw_indexed needs a bound index buffer\n");
        return;
    }
    if (!prepare_draw(renderer, "at_renderer_draw_indexed")) {
        return;
    }

    vkCmdDrawIndexed(at_renderer_command_buffer(renderer), index_count, 1, 0, 0, 0);
}