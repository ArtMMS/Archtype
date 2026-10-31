/*
 * Spin example: three quads rotating in 3D.
 *   - The camera (view * projection) goes in a uniform buffer, once per frame.
 *   - Each quad's model matrix and tint go in push constants, once per draw.
 */

#include <stddef.h>
#include <stdio.h>

#include <archtype/archtype.h>

#define WINDOW_TITLE "Archtype - Spin"
#define QUAD_COUNT   3

/* Must match the inputs of spin.vert */
typedef struct vertex {
    float position[3];
    float color[3];
} vertex;

/* Must match "uniform Camera" in spin.vert (std140: a mat4 needs no padding) */
typedef struct camera_uniforms {
    at_mat4 view_projection;
} camera_uniforms;

/* Must match "push_constant Object" in spin.vert (80 bytes, max 128) */
typedef struct object_constants {
    at_mat4 model;
    float   tint[4];
} object_constants;

/* A 1x1 quad on the XY plane, in world space (+Y is up) */
static const vertex k_vertices[] = {
    { { -0.5f,  0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, /* 0: top left     */
    { { -0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f } }, /* 1: bottom left  */
    { {  0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f } }, /* 2: bottom right */
    { {  0.5f,  0.5f, 0.0f }, { 1.0f, 1.0f, 0.0f } }, /* 3: top right    */
};

static const uint16_t k_indices[] = { 0, 1, 2, 2, 3, 0 };

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
        .app_name          = "Spin Example",
        .enable_validation = true,
        .vsync             = true,
    };

    at_renderer *renderer = at_renderer_create(&renderer_desc);
    if (!renderer) {
        at_window_destroy(window);
        at_platform_shutdown();
        return 1;
    }

    /* Buffers */
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
    at_buffer_desc camera_desc = {
        .type = AT_BUFFER_UNIFORM,
        .size = sizeof(camera_uniforms),
    };

    at_buffer *vertex_buffer = at_buffer_create(renderer, &vertex_desc);
    at_buffer *index_buffer  = at_buffer_create(renderer, &index_desc);
    at_buffer *camera_buffer = at_buffer_create(renderer, &camera_desc);

    /* Pipeline */
    at_shader *vertex_shader   = at_shader_load(renderer, "shaders/spin.vert.spv");
    at_shader *fragment_shader = at_shader_load(renderer, "shaders/spin.frag.spv");

    at_pipeline *pipeline = NULL;
    if (vertex_shader && fragment_shader) {
        at_pipeline_desc pipeline_desc = {
            .vertex_shader   = vertex_shader,
            .fragment_shader = fragment_shader,
            .vertex_layout   = {
                .stride          = sizeof(vertex),
                .attribute_count = 2,
                .attributes      = {
                    { .location = 0, .format = AT_VERTEX_FORMAT_FLOAT3,
                      .offset = offsetof(vertex, position) },
                    { .location = 1, .format = AT_VERTEX_FORMAT_FLOAT3,
                      .offset = offsetof(vertex, color) },
                },
            },
            .binding_count = 1,
            .bindings      = {
                { .binding = 0, .type = AT_BINDING_UNIFORM_BUFFER,
                  .stages = AT_SHADER_STAGE_VERTEX },
            },
            .push_constant_size = sizeof(object_constants),
            .cull_mode          = AT_CULL_NONE, /* Show both faces while spinning */
        };
        pipeline = at_pipeline_create(renderer, &pipeline_desc);
    }

    at_shader_destroy(renderer, vertex_shader);
    at_shader_destroy(renderer, fragment_shader);

    if (pipeline && vertex_buffer && index_buffer && camera_buffer) {
        at_renderer_set_clear_color(renderer, 0.08f, 0.08f, 0.10f, 1.0f);

        /* Camera 3 units in front of the origin, looking at it */
        at_mat4 view = at_mat4_look_at(at_vec3_make(0.0f, 0.0f, 3.0f),
                                       at_vec3_zero(), at_vec3_up());

        const float tints[QUAD_COUNT][4] = {
            { 1.0f, 0.6f, 0.6f, 1.0f },
            { 1.0f, 1.0f, 1.0f, 1.0f },
            { 0.6f, 0.6f, 1.0f, 1.0f },
        };

        while (!at_window_should_close(window)) {
            at_platform_poll_events();

            if (at_window_is_minimized(window)) {
                at_platform_wait_events();
                continue;
            }

            if (!at_renderer_begin_frame(renderer)) {
                continue;
            }

            float time = (float)at_platform_get_time();

            /* Camera: once per frame, with the current aspect ratio */
            uint32_t width  = 0;
            uint32_t height = 0;
            at_renderer_get_size(renderer, &width, &height);

            at_mat4 projection = at_mat4_perspective(at_radians(60.0f),
                                                     (float)width / (float)height,
                                                     0.1f, 100.0f);
            camera_uniforms camera = { at_mat4_mul(projection, view) };
            at_buffer_update(renderer, camera_buffer, &camera, sizeof(camera), 0);

            at_renderer_bind_pipeline(renderer, pipeline);
            at_renderer_bind_uniform_buffer(renderer, 0, camera_buffer);
            at_renderer_bind_vertex_buffer(renderer, vertex_buffer);
            at_renderer_bind_index_buffer(renderer, index_buffer);

            /* Objects: one push constant block per draw */
            for (int i = 0; i < QUAD_COUNT; ++i) {
                at_transform transform = at_transform_identity();
                transform.position = at_vec3_make((float)(i - 1) * 1.4f, 0.0f, 0.0f);
                transform.rotation = at_quat_from_axis_angle(at_vec3_up(),
                                                             time * (0.8f + 0.4f * (float)i));

                object_constants object;
                object.model   = at_transform_to_mat4(transform);
                object.tint[0] = tints[i][0];
                object.tint[1] = tints[i][1];
                object.tint[2] = tints[i][2];
                object.tint[3] = tints[i][3];

                at_renderer_push_constants(renderer, &object, sizeof(object));
                at_renderer_draw_indexed(renderer, 6);
            }

            at_renderer_end_frame(renderer);
        }
    }

    /* Destroy in reverse order of creation */
    at_pipeline_destroy(renderer, pipeline);
    at_buffer_destroy(renderer, camera_buffer);
    at_buffer_destroy(renderer, index_buffer);
    at_buffer_destroy(renderer, vertex_buffer);
    at_renderer_destroy(renderer);
    at_window_destroy(window);
    at_platform_shutdown();
    return 0;
}