#include "archtype/renderer/renderer.h"

#include <stdio.h>
#include <stdlib.h>

#include "renderer/vulkan/vk_context.h"

struct at_renderer {
    at_window     *window;
    at_vk_context  context;
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

    at_vk_context_desc context_desc;
    context_desc.app_name          = desc->app_name;
    context_desc.enable_validation = desc->enable_validation;

    if (!at_vk_context_init(&renderer->context, &context_desc)) {
        free(renderer);
        return NULL;
    }

    return renderer;
}

void at_renderer_destroy(at_renderer *renderer) {
    if (!renderer) {
        return;
    }

    at_vk_context_shutdown(&renderer->context);
    free(renderer);
}