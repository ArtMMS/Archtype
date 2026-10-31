/*
 * Texture example: two spinning quads.
 *   Left:  an 8x8 checkerboard generated in code, sharp pixels (nearest filter).
 *   Right: an image loaded from bin/textures/image.png (smooth, linear filter).
 *
 * To use your own image, put a PNG at examples/c/texture/image.png and run
 * CMake again. Without it, the checkerboard is used on both quads.
 */

#include <stddef.h>
#include <stdio.h>

#include <archtype/archtype.h>

#define WINDOW_TITLE  "Archtype - Texture"
#define CHECKER_SIZE  8

/* Must match the inputs of texture.vert */
typedef struct vertex {
    float position[3];
    float uv[2];
} vertex;

typedef struct camera_uniforms {
    at_mat4 view_projection;
} camera_uniforms;

typedef struct object_constants {
    at_mat4 model;
} object_constants;

/* A 1x1 quad on the XY plane. UV (0, 0) is the top-left of the image. */
static const vertex k_vertices[] = {
    { { -0.5f,  0.5f, 0.0f }, { 0.0f, 0.0f } }, /* 0: top left     */
    { { -0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f } }, /* 1: bottom left  */
    { {  0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f } }, /* 2: bottom right */
    { {  0.5f,  0.5f, 0.0f }, { 1.0f, 0.0f } }, /* 3: top right    */
};

static const uint16_t k_indices[] = { 0, 1, 2, 2, 3, 0 };

/* RGBA checkerboard, 4 bytes per pixel */
static at_texture *create_checker_texture(at_renderer *renderer) {
    uint8_t pixels[CHECKER_SIZE * CHECKER_SIZE * 4];

    for (int y = 0; y < CHECKER_SIZE; ++y) {
        for (int x = 0; x < CHECKER_SIZE; ++x) {
            uint8_t *p     = &pixels[(y * CHECKER_SIZE + x) * 4];
            bool     light = ((x + y) % 2) == 0;

            p[0] = light ? 240 : 40;  /* R */
            p[1] = light ? 200 : 40;  /* G */
            p[2] = light ? 60  : 90;  /* B */
            p[3] = 255;               /* A */
        }
    }

    at_texture_options options = { .filter = AT_TEXTURE_FILTER_NEAREST };
    return at_texture_create(renderer, CHECKER_SIZE, CHECKER_SIZE, pixels, &options);
}

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
        .app_name          = "Texture Example",
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

    /* Textures */
    at_texture *checker = create_checker_texture(renderer);
    at_texture *image   = at_texture_load(renderer, "textures/image.png", NULL);
    if (!image) {
        printf("No textures/image.png found: using the checkerboard on both quads\n");
    }

    /* Pipeline: camera at binding 0, texture at binding 1 */
    at_shader *vertex_shader   = at_shader_load(renderer, "shaders/texture.vert.spv");
    at_shader *fragment_shader = at_shader_load(renderer, "shaders/texture.frag.spv");

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
                    { .location = 1, .format = AT_VERTEX_FORMAT_FLOAT2,
                      .offset = offsetof(vertex, uv) },
                },
            },
            .binding_count = 2,
            .bindings      = {
                { .binding = 0, .type = AT_BINDING_UNIFORM_BUFFER,
                  .stages = AT_SHADER_STAGE_VERTEX },
                { .binding = 1, .type = AT_BINDING_TEXTURE,
                  .stages = AT_SHADER_STAGE_FRAGMENT },
            },
            .push_constant_size = sizeof(object_constants),
            .cull_mode          = AT_CULL_NONE,
            .blend_mode         = AT_BLEND_ALPHA, /* Respect transparent PNGs */
        };
        pipeline = at_pipeline_create(renderer, &pipeline_desc);
    }

    at_shader_destroy(renderer, vertex_shader);
    at_shader_destroy(renderer, fragment_shader);

    if (pipeline && vertex_buffer && index_buffer && camera_buffer && checker) {
        at_renderer_set_clear_color(renderer, 0.08f, 0.08f, 0.10f, 1.0f);

        at_mat4 view = at_mat4_look_at(at_vec3_make(0.0f, 0.0f, 2.5f),
                                       at_vec3_zero(), at_vec3_up());

        at_texture *textures[2] = { checker, image ? image : checker };

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

            for (int i = 0; i < 2; ++i) {
                at_transform transform = at_transform_identity();
                transform.position = at_vec3_make(i == 0 ? -0.7f : 0.7f, 0.0f, 0.0f);
                transform.rotation = at_quat_from_axis_angle(at_vec3_up(), time * 0.6f);

                object_constants object = { at_transform_to_mat4(transform) };

                at_renderer_bind_texture(renderer, 1, textures[i]);
                at_renderer_push_constants(renderer, &object, sizeof(object));
                at_renderer_draw_indexed(renderer, 6);
            }

            at_renderer_end_frame(renderer);
        }
    }

    /* Destroy in reverse order of creation */
    at_pipeline_destroy(renderer, pipeline);
    at_texture_destroy(renderer, image);
    at_texture_destroy(renderer, checker);
    at_buffer_destroy(renderer, camera_buffer);
    at_buffer_destroy(renderer, index_buffer);
    at_buffer_destroy(renderer, vertex_buffer);
    at_renderer_destroy(renderer);
    at_window_destroy(window);
    at_platform_shutdown();
    return 0;
}