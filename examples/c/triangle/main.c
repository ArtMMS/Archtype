#include <stdio.h>

#include <archtype/archtype.h>

#define WINDOW_TITLE "Archtype - Triangle"

int main(void) {
    if (!at_platform_init()) {
        return 1;
    }

    at_window_desc window_desc = {
        .title     = WINDOW_TITLE,
        .width     = 1280,
        .height    = 720,
        .resizable = true,
    };

    at_window *window = at_window_create(&window_desc);
    if (!window) {
        at_platform_shutdown();
        return 1;
    }

    at_renderer_desc renderer_desc = {
        .window            = window,
        .app_name          = "Triangle Example",
        .enable_validation = true,
        .vsync             = true,
    };

    at_renderer *renderer = at_renderer_create(&renderer_desc);
    if (!renderer) {
        at_window_destroy(window);
        at_platform_shutdown();
        return 1;
    }

    /* Shaders are compiled by CMake into bin/shaders/ */
    at_shader *vertex_shader   = at_shader_load(renderer, "shaders/triangle.vert.spv");
    at_shader *fragment_shader = at_shader_load(renderer, "shaders/triangle.frag.spv");

    at_pipeline *pipeline = NULL;
    if (vertex_shader && fragment_shader) {
        at_pipeline_desc pipeline_desc = {
            .vertex_shader   = vertex_shader,
            .fragment_shader = fragment_shader,
            .cull_mode       = AT_CULL_BACK, /* The triangle faces the camera */
        };
        pipeline = at_pipeline_create(renderer, &pipeline_desc);
    }

    /* The pipeline keeps what it needs: shaders can be destroyed now */
    at_shader_destroy(renderer, vertex_shader);
    at_shader_destroy(renderer, fragment_shader);

    if (!pipeline) {
        at_renderer_destroy(renderer);
        at_window_destroy(window);
        at_platform_shutdown();
        return 1;
    }

    at_renderer_set_clear_color(renderer, 0.08f, 0.08f, 0.10f, 1.0f);

    while (!at_window_should_close(window)) {
        at_platform_poll_events();

        if (at_window_is_minimized(window)) {
            at_platform_wait_events();
            continue;
        }

        if (at_renderer_begin_frame(renderer)) {
            at_renderer_bind_pipeline(renderer, pipeline);
            at_renderer_draw(renderer, 3);
            at_renderer_end_frame(renderer);
        }
    }

    /* Destroy in reverse order of creation */
    at_pipeline_destroy(renderer, pipeline);
    at_renderer_destroy(renderer);
    at_window_destroy(window);
    at_platform_shutdown();
    return 0;
}