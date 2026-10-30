#include "archtype/renderer/renderer.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "renderer/vulkan/vk_context.h"
#include "renderer/vulkan/vk_frame.h"
#include "renderer/vulkan/vk_swapchain.h"

struct at_renderer {
    at_window       *window;
    bool             vsync;
    float            clear_color[4]; /* sRGB, as given by the user */
    bool             frame_started;

    at_vk_context    context;
    at_vk_swapchain  swapchain;
    at_vk_frames     frames;
};

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

/* ------------------------------------------------------------------------ */
/* Public functions                                                          */
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

    bool ok = at_vk_swapchain_create(&renderer->swapchain, &renderer->context,
                                     (uint32_t)width, (uint32_t)height, renderer->vsync)
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

    /* Wait for the GPU before destroying anything it may still be using */
    if (renderer->context.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(renderer->context.device);
        at_vk_frames_destroy(&renderer->frames, &renderer->context);
        at_vk_swapchain_destroy(&renderer->swapchain, &renderer->context);
    }

    at_vk_context_shutdown(&renderer->context);
    free(renderer);
}

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

    /* Swapchain recreation (window resize) will be handled here later */
    if (renderer->frames.out_of_date) {
        return false;
    }

    float clear[4];
    for (int i = 0; i < 3; ++i) {
        clear[i] = is_srgb_format(renderer->swapchain.format)
                 ? srgb_to_linear(renderer->clear_color[i])
                 : renderer->clear_color[i];
    }
    clear[3] = renderer->clear_color[3]; /* Alpha is always linear */

    if (!at_vk_frame_begin(&renderer->frames, &renderer->context,
                           &renderer->swapchain, clear)) {
        return false;
    }

    renderer->frame_started = true;
    return true;
}

void at_renderer_end_frame(at_renderer *renderer) {
    if (!renderer->frame_started) {
        fprintf(stderr, "[archtype] at_renderer_end_frame called without begin_frame\n");
        return;
    }

    at_vk_frame_end(&renderer->frames, &renderer->context, &renderer->swapchain);
    renderer->frame_started = false;
}