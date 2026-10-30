#include "archtype/renderer/renderer.h"

#include <stdio.h>
#include <stdlib.h>

#include "renderer/vulkan/vk_context.h"
#include "renderer/vulkan/vk_swapchain.h"

struct at_renderer {
    at_window       *window;
    bool             vsync;
    at_vk_context    context;
    at_vk_swapchain  swapchain;
};

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

    renderer->window = desc->window;
    renderer->vsync  = desc->vsync;

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

    if (!at_vk_swapchain_create(&renderer->swapchain, &renderer->context,
                                (uint32_t)width, (uint32_t)height, renderer->vsync)) {
        at_vk_context_shutdown(&renderer->context);
        free(renderer);
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
    }

    /* Destroy in reverse order of creation */
    at_vk_swapchain_destroy(&renderer->swapchain, &renderer->context);
    at_vk_context_shutdown(&renderer->context);
    free(renderer);
}