#include <stddef.h>
#include <stdio.h>

#include <archtype/archtype.h>

#define WINDOW_TITLE "Archtype - Quad"

/* Must match the inputs of quad.vert */
typedef struct vertex {
    float position[2];
    float color[3];
} vertex;

/* Positions in clip space: Vulkan's Y axis points down. */
static const vertex k_vertices[] = {
    { { -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f } }, /* 0: top left     - red    */
    { { -0.5f,  0.5f }, { 0.0f, 1.0f, 0.0f } }, /* 1: bottom left  - green  */
    { {  0.5f,  0.5f }, { 0.0f, 0.0f, 1.0f } }, /* 2: bottom right - blue   */
    { {  0.5f, -0.5f }, { 1.0f, 1.0f, 0.0f } }, /* 3: top right    - yellow */
};

/* Two triangles sharing vertices 0 and 2, counter-clockwise on screen */
static const uint16_t k_indices[] = {
    0, 1, 2,
    2, 3, 0,
};

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
        .app_name          = "Quad Example",
        .enable_validation = true,
        .vsync             = true,
    };

    at_renderer *renderer = at_renderer_create(&renderer_desc);
    if (!renderer) {
        at_window_destroy(window);
        at_platform_shutdown();
        return 1;
    }

    /* Buffers: uploaded once to fast GPU memory */
    at_buffer_desc vertex_desc = {
        .type = AT_BUFFER_VERTEX,
        .size = sizeof(k_vertices),
        .data = k_vertices,
    };
    at_buffer_desc index_desc = {
        .type       = AT_BUFFER_INDEX,
        .index_type = AT_INDEX_UINT16,
        .size       = sizeof(k_indices),
        .data       = k_indices,
    };

    at_buffer *vertex_buffer = at_buffer_create(renderer, &vertex_desc);
    at_buffer *index_buffer  = at_buffer_create(renderer, &index_desc);

    /* Pipeline */
    at_shader *vertex_shader   = at_shader_load(renderer, "shaders/quad.vert.spv");
    at_shader *fragment_shader = at_shader_load(renderer, "shaders/quad.frag.spv");

    at_pipeline *pipeline = NULL;
    if (vertex_shader && fragment_shader) {
        at_pipeline_desc pipeline_desc = {
            .vertex_shader   = vertex_shader,
            .fragment_shader = fragment_shader,
            .vertex_layout   = {
                .stride          = sizeof(vertex),
                .attribute_count = 2,
                .attributes      = {
                    { .location = 0, .format = AT_VERTEX_FORMAT_FLOAT2,
                      .offset = offsetof(vertex, position) },
                    { .location = 1, .format = AT_VERTEX_FORMAT_FLOAT3,
                      .offset = offsetof(vertex, color) },
                },
            },
            .cull_mode = AT_CULL_BACK,
        };
        pipeline = at_pipeline_create(renderer, &pipeline_desc);
    }

    at_shader_destroy(renderer, vertex_shader);
    at_shader_destroy(renderer, fragment_shader);

    if (pipeline && vertex_buffer && index_buffer) {
        at_renderer_set_clear_color(renderer, 0.08f, 0.08f, 0.10f, 1.0f);

        while (!at_window_should_close(window)) {
            at_platform_poll_events();

            if (at_window_is_minimized(window)) {
                at_platform_wait_events();
                continue;
            }

            if (at_renderer_begin_frame(renderer)) {
                at_renderer_bind_pipeline(renderer, pipeline);
                at_renderer_bind_vertex_buffer(renderer, vertex_buffer);
                at_renderer_bind_index_buffer(renderer, index_buffer);
                at_renderer_draw_indexed(renderer, 6);
                at_renderer_end_frame(renderer);
            }
        }
    }

    /* Destroy in reverse order of creation */
    at_pipeline_destroy(renderer, pipeline);
    at_buffer_destroy(renderer, index_buffer);
    at_buffer_destroy(renderer, vertex_buffer);
    at_renderer_destroy(renderer);
    at_window_destroy(window);
    at_platform_shutdown();
    return 0;
}