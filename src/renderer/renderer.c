#include "renderer/renderer_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

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
           && at_vk_frames_create(&renderer->frames, &renderer->context);

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
        at_vk_frames_destroy(&renderer->frames, &renderer->context);
        at_vk_swapchain_destroy(&renderer->swapchain, &renderer->context);
        at_vk_upload_destroy(&renderer->upload, &renderer->context);
        at_vk_allocator_destroy(&renderer->allocator);
    }

    at_vk_context_shutdown(&renderer->context);
    free(renderer);
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

    renderer->frame_started      = true;
    renderer->bound_pipeline     = NULL;
    renderer->index_buffer_bound = false;
    set_viewport_and_scissor(renderer);
    return true;
}

void at_renderer_end_frame(at_renderer *renderer) {
    if (!renderer->frame_started) {
        fprintf(stderr, "[archtype] at_renderer_end_frame called without begin_frame\n");
        return;
    }

    at_vk_frame_end(&renderer->frames, &renderer->context, &renderer->swapchain);
    renderer->frame_started      = false;
    renderer->bound_pipeline     = NULL;
    renderer->index_buffer_bound = false;
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
    renderer->bound_pipeline = pipeline;
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

void at_renderer_draw(at_renderer *renderer, uint32_t vertex_count) {
    if (!check_recording(renderer, "at_renderer_draw")) {
        return;
    }
    if (!renderer->bound_pipeline) {
        fprintf(stderr, "[archtype] at_renderer_draw called without a bound pipeline\n");
        return;
    }

    vkCmdDraw(at_renderer_command_buffer(renderer), vertex_count, 1, 0, 0);
}

void at_renderer_draw_indexed(at_renderer *renderer, uint32_t index_count) {
    if (!check_recording(renderer, "at_renderer_draw_indexed")) {
        return;
    }
    if (!renderer->bound_pipeline || !renderer->index_buffer_bound) {
        fprintf(stderr, "[archtype] at_renderer_draw_indexed needs a bound pipeline "
                        "and index buffer\n");
        return;
    }

    vkCmdDrawIndexed(at_renderer_command_buffer(renderer), index_count, 1, 0, 0, 0);
}