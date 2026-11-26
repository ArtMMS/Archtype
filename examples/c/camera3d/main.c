/*
 * 3D camera example: perspective projection, a free fly camera and the
 * depth buffer.
 *   Right mouse button (hold): look around
 *   W A S D:                   move
 *   E or Space / Q or Ctrl:    up / down
 *   Shift (hold):              faster
 *   Mouse wheel:               change the speed
 *   Tab:                       turn the depth test on and off
 *   - and =:                   narrower / wider field of view
 *   R:                         reset the camera
 *   Esc:                       quit
 *
 * The boxes use the example's own shaders (simple shading and fog); the
 * engine's 3D renderer comes in the next part of the roadmap. The HUD is
 * drawn on top with the 2D renderer.
 */

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include <archtype/archtype.h>

#define WINDOW_TITLE "Archtype - Camera 3D"

#define GRID_SIZE    13   /* Pillars per side           */
#define GRID_SPACING 4.0f /* World units between pillars */
#define MIN_FOV      20.0f
#define MAX_FOV      120.0f

/* ------------------------------------------------------------------------ */
/* Data shared with the shaders                                              */
/* ------------------------------------------------------------------------ */

/* Must match the inputs of camera3d.vert */
typedef struct vertex {
    float position[3];
    float normal[3];
} vertex;

/* Must match "uniform Scene" in the shaders (std140: only mat4 and vec4) */
typedef struct scene_uniforms {
    at_mat4 view_projection;
    float   camera_position[4];
    float   fog_color[4];       /* a = fog density   */
    float   light_direction[4];
} scene_uniforms;

/* Must match "push_constant Object" in the shaders (80 bytes, max 128) */
typedef struct object_constants {
    at_mat4 model;
    float   color[4];           /* a = checkerboard  */
} object_constants;

/* ------------------------------------------------------------------------ */
/* Cube                                                                      */
/* ------------------------------------------------------------------------ */

#define CUBE_VERTEX_COUNT 24 /* 4 per face: each face has its own normal */
#define CUBE_INDEX_COUNT  36

/* A 1x1x1 cube centered on the origin. Every face is built from its normal */
/* and two axes u and v with u x v = normal, so its triangles go counter-   */
/* clockwise seen from outside: the front faces for AT_CULL_BACK.           */
static void build_cube(vertex vertices[CUBE_VERTEX_COUNT], uint16_t indices[CUBE_INDEX_COUNT]) {
    static const float faces[6][3][3] = {
        /*  normal               u                    v                */
        { {  1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f,  0.0f } }, /* +X */
        { { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f,  1.0f }, { 0.0f, 1.0f,  0.0f } }, /* -X */
        { {  0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f,  0.0f }, { 0.0f, 0.0f, -1.0f } }, /* +Y */
        { {  0.0f,-1.0f, 0.0f }, { 1.0f, 0.0f,  0.0f }, { 0.0f, 0.0f,  1.0f } }, /* -Y */
        { {  0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f,  0.0f }, { 0.0f, 1.0f,  0.0f } }, /* +Z */
        { {  0.0f, 0.0f,-1.0f }, {-1.0f, 0.0f,  0.0f }, { 0.0f, 1.0f,  0.0f } }, /* -Z */
    };
    static const float corners[4][2] = { { -1.0f, -1.0f }, { 1.0f, -1.0f },
                                         {  1.0f,  1.0f }, { -1.0f, 1.0f } };

    for (int f = 0; f < 6; ++f) {
        const float *n = faces[f][0];
        const float *u = faces[f][1];
        const float *v = faces[f][2];

        for (int c = 0; c < 4; ++c) {
            vertex *out = &vertices[f * 4 + c];
            for (int i = 0; i < 3; ++i) {
                out->position[i] = 0.5f * (n[i] + corners[c][0] * u[i] + corners[c][1] * v[i]);
                out->normal[i]   = n[i];
            }
        }

        const uint16_t base = (uint16_t)(f * 4);
        uint16_t      *tri  = &indices[f * 6];
        tri[0] = base;
        tri[1] = (uint16_t)(base + 1);
        tri[2] = (uint16_t)(base + 2);
        tri[3] = base;
        tri[4] = (uint16_t)(base + 2);
        tri[5] = (uint16_t)(base + 3);
    }
}

/* ------------------------------------------------------------------------ */
/* Scene                                                                     */
/* ------------------------------------------------------------------------ */

static void draw_box(at_renderer *renderer, at_vec3 position, at_vec3 size, at_quat rotation,
                     at_color color, float checker) {
    at_transform transform = at_transform_identity();
    transform.position     = position;
    transform.rotation     = rotation;
    transform.scale        = size;

    object_constants object;
    object.model    = at_transform_to_mat4(transform);
    object.color[0] = color.r;
    object.color[1] = color.g;
    object.color[2] = color.b;
    object.color[3] = checker;

    at_renderer_push_constants(renderer, &object, sizeof(object));
    at_renderer_draw_indexed(renderer, CUBE_INDEX_COUNT);
}

static void draw_scene(at_renderer *renderer, float time) {
    const at_quat no_rotation = at_quat_identity();

    /* Floor, with a checkerboard of 1x1 tiles */
    draw_box(renderer, at_vec3_make(0.0f, -0.1f, 0.0f), at_vec3_make(80.0f, 0.2f, 80.0f),
             no_rotation, at_color_hex(0x5A6070FF), 0.25f);

    /* World axes at the origin: X red, Y green, Z blue (pointing to +) */
    draw_box(renderer, at_vec3_make(1.5f, 0.05f, 0.0f), at_vec3_make(3.0f, 0.1f, 0.1f),
             no_rotation, at_color_hex(0xE04848FF), 0.0f);
    draw_box(renderer, at_vec3_make(0.0f, 1.5f, 0.0f), at_vec3_make(0.1f, 3.0f, 0.1f),
             no_rotation, at_color_hex(0x7DCE82FF), 0.0f);
    draw_box(renderer, at_vec3_make(0.0f, 0.05f, 1.5f), at_vec3_make(0.1f, 0.1f, 3.0f),
             no_rotation, at_color_hex(0x4D9DE0FF), 0.0f);

    /* A field of pillars with different heights, around the middle */
    const at_color palette[5] = {
        at_color_hex(0xF2C14EFF), at_color_hex(0xE15A97FF), at_color_hex(0x4D9DE0FF),
        at_color_hex(0x7DCE82FF), at_color_hex(0xF78154FF),
    };
    const int half = GRID_SIZE / 2;

    for (int z = -half; z <= half; ++z) {
        for (int x = -half; x <= half; ++x) {
            if (x >= -1 && x <= 1 && z >= -1 && z <= 1) {
                continue; /* Leave room in the middle */
            }

            float height = 0.5f + 5.0f * (0.5f + 0.5f * sinf((float)x * 0.7f)
                                                       * cosf((float)z * 0.55f));
            at_vec3 position = at_vec3_make((float)x * GRID_SPACING, height * 0.5f,
                                            (float)z * GRID_SPACING);
            draw_box(renderer, position, at_vec3_make(1.2f, height, 1.2f), no_rotation,
                     palette[(x + z + 2 * GRID_SIZE) % 5], 0.0f);
        }
    }

    /* Two spinning cubes that go through each other: with the depth test */
    /* the intersection is correct; without it, the last one drawn wins   */
    at_quat spin_a = at_quat_from_axis_angle(at_vec3_make(0.3f, 1.0f, 0.2f), time * 0.7f);
    at_quat spin_b = at_quat_from_axis_angle(at_vec3_make(1.0f, 0.4f, -0.3f), time * -0.9f);
    draw_box(renderer, at_vec3_make(-0.4f, 2.5f, 0.0f), at_vec3_make(1.8f, 1.8f, 1.8f), spin_a,
             at_color_hex(0xF2C14EFF), 0.0f);
    draw_box(renderer, at_vec3_make(0.6f, 2.7f, 0.3f), at_vec3_make(1.5f, 1.5f, 1.5f), spin_b,
             at_color_hex(0xE15A97FF), 0.0f);
}

/* ------------------------------------------------------------------------ */
/* Camera                                                                    */
/* ------------------------------------------------------------------------ */

static void reset_camera(at_camera3d *camera, at_fly_camera *fly) {
    *camera          = at_camera3d_make();
    camera->position = at_vec3_make(0.0f, 6.0f, 22.0f);
    at_camera3d_look_at(camera, at_vec3_make(0.0f, 1.5f, 0.0f), at_vec3_up());

    float speed = fly->speed; /* Keep the chosen speed */
    *fly        = at_fly_camera_make();
    fly->speed  = speed;
    at_fly_camera_sync(fly, camera);
}

/* ------------------------------------------------------------------------ */
/* HUD                                                                       */
/* ------------------------------------------------------------------------ */

static void draw_hud(at_renderer2d *r2d, const at_font *font, float width, float height,
                     const at_camera3d *camera, const at_fly_camera *fly, bool depth_test,
                     uint32_t fps) {
    const float scale   = 2.0f;
    const float margin  = 8.0f;
    const float padding = 8.0f;

    char text[512];
    snprintf(text, sizeof(text),
             "%u FPS   depth test %s (Tab)\n"
             "Position (%.1f, %.1f, %.1f)   yaw %.0f   pitch %.0f\n"
             "Speed %.1f   field of view %.0f (- and =)\n"
             "\n"
             "Hold right mouse: look   WASD: move   E/Q: up/down\n"
             "Shift: faster   Wheel: speed   R: reset   Esc: quit",
             (unsigned)fps, depth_test ? "ON" : "OFF",
             (double)camera->position.x, (double)camera->position.y,
             (double)camera->position.z, (double)at_degrees(fly->yaw),
             (double)at_degrees(fly->pitch), (double)fly->speed,
             (double)at_degrees(camera->fov_y));

    /* Box in the top-left corner (the 2D camera has the origin in the */
    /* middle of the screen and +Y up); the text starts at its top-left */
    at_vec2 size = at_font_measure(font, text, scale);
    at_vec2 box  = at_vec2_make(size.x + 2.0f * padding, size.y + 2.0f * padding);
    float   left = -width * 0.5f + margin;
    float   top  =  height * 0.5f - margin;

    at_renderer2d_draw_rect(r2d, at_vec2_make(left + box.x * 0.5f, top - box.y * 0.5f), box,
                            0.0f, at_color_rgba(0.0f, 0.0f, 0.0f, 0.55f));
    at_renderer2d_draw_text(r2d, font, text, at_vec2_make(left + padding, top - padding),
                            scale, at_color_white());

    /* Crosshair while looking around */
    if (fly->looking) {
        at_renderer2d_draw_rect(r2d, at_vec2_zero(), at_vec2_make(12.0f, 2.0f), 0.0f,
                                at_color_white());
        at_renderer2d_draw_rect(r2d, at_vec2_zero(), at_vec2_make(2.0f, 12.0f), 0.0f,
                                at_color_white());
    }
}

/* ------------------------------------------------------------------------ */
/* Main                                                                      */
/* ------------------------------------------------------------------------ */

/* Pipeline for the boxes, with or without the depth test */
static at_pipeline *create_box_pipeline(at_renderer *renderer, at_shader *vertex_shader,
                                        at_shader *fragment_shader, bool depth_test) {
    at_pipeline_desc desc = {
        .vertex_shader   = vertex_shader,
        .fragment_shader = fragment_shader,
        .vertex_layout   = {
            .stride          = sizeof(vertex),
            .attribute_count = 2,
            .attributes      = {
                { .location = 0, .format = AT_VERTEX_FORMAT_FLOAT3,
                  .offset = offsetof(vertex, position) },
                { .location = 1, .format = AT_VERTEX_FORMAT_FLOAT3,
                  .offset = offsetof(vertex, normal) },
            },
        },
        .binding_count = 1,
        .bindings      = {
            { .binding = 0, .type = AT_BINDING_UNIFORM_BUFFER }, /* Vertex and fragment */
        },
        .push_constant_size = sizeof(object_constants),
        .cull_mode          = AT_CULL_BACK, /* Closed boxes: skip the faces inside */
        .depth_test         = depth_test,
        .depth_write        = depth_test,
    };
    return at_pipeline_create(renderer, &desc);
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
        .app_name          = "Camera 3D Example",
        .enable_validation = true,
        .vsync             = true,
    };

    at_renderer *renderer = at_renderer_create(&renderer_desc);
    if (!renderer) {
        at_window_destroy(window);
        at_platform_shutdown();
        return 1;
    }

    /* Cube buffers */
    vertex   cube_vertices[CUBE_VERTEX_COUNT];
    uint16_t cube_indices[CUBE_INDEX_COUNT];
    build_cube(cube_vertices, cube_indices);

    at_buffer_desc vertex_desc = {
        .type = AT_BUFFER_VERTEX,
        .size = sizeof(cube_vertices),
        .data = cube_vertices,
    };
    at_buffer_desc index_desc = {
        .type       = AT_BUFFER_INDEX,
        .index_type = AT_INDEX_UINT16,
        .size       = sizeof(cube_indices),
        .data       = cube_indices,
    };
    at_buffer_desc scene_desc = {
        .type = AT_BUFFER_UNIFORM,
        .size = sizeof(scene_uniforms),
    };

    at_buffer *vertex_buffer = at_buffer_create(renderer, &vertex_desc);
    at_buffer *index_buffer  = at_buffer_create(renderer, &index_desc);
    at_buffer *scene_buffer  = at_buffer_create(renderer, &scene_desc);

    /* Pipelines: one with the depth test, one without, to compare */
    at_shader *vertex_shader   = at_shader_load(renderer, "shaders/camera3d.vert.spv");
    at_shader *fragment_shader = at_shader_load(renderer, "shaders/camera3d.frag.spv");

    at_pipeline *depth_pipeline    = NULL;
    at_pipeline *no_depth_pipeline = NULL;
    if (vertex_shader && fragment_shader) {
        depth_pipeline    = create_box_pipeline(renderer, vertex_shader, fragment_shader, true);
        no_depth_pipeline = create_box_pipeline(renderer, vertex_shader, fragment_shader, false);
    }

    at_shader_destroy(renderer, vertex_shader);
    at_shader_destroy(renderer, fragment_shader);

    /* HUD */
    at_renderer2d *r2d  = at_renderer2d_create(renderer, NULL);
    at_font       *font = at_font_create_default(renderer);

    if (depth_pipeline && no_depth_pipeline && vertex_buffer && index_buffer && scene_buffer &&
        r2d && font) {
        const at_color sky = at_color_hex(0x9CB8D8FF);
        at_renderer_set_clear_color(renderer, sky.r, sky.g, sky.b, 1.0f);

        at_camera3d   camera = at_camera3d_make();
        at_fly_camera fly    = at_fly_camera_make();
        reset_camera(&camera, &fly);

        at_camera2d hud_camera = at_camera2d_make();
        bool        depth_test = true;
        double      last_time  = at_platform_get_time();
        double      fps_timer  = 0.0;
        uint32_t    frames     = 0;
        uint32_t    fps        = 0;

        while (!at_window_should_close(window)) {
            at_platform_poll_events();

            if (at_window_is_minimized(window)) {
                at_platform_wait_events();
                last_time = at_platform_get_time();
                continue;
            }

            double now = at_platform_get_time();
            float  dt  = (float)(now - last_time);
            last_time  = now;

            fps_timer += dt;
            frames++;
            if (fps_timer >= 1.0) {
                fps       = frames;
                frames    = 0;
                fps_timer = 0.0;
            }

            /* Input */
            if (at_window_was_key_pressed(window, AT_KEY_ESCAPE)) {
                at_window_request_close(window);
            }
            if (at_window_was_key_pressed(window, AT_KEY_TAB)) {
                depth_test = !depth_test;
            }
            if (at_window_was_key_pressed(window, AT_KEY_R)) {
                reset_camera(&camera, &fly);
            }
            if (at_window_is_key_down(window, AT_KEY_MINUS)) {
                camera.fov_y -= at_radians(40.0f) * dt;
            }
            if (at_window_is_key_down(window, AT_KEY_EQUALS)) {
                camera.fov_y += at_radians(40.0f) * dt;
            }
            camera.fov_y = at_clampf(camera.fov_y, at_radians(MIN_FOV), at_radians(MAX_FOV));

            at_fly_camera_update(&fly, &camera, window, dt);

            if (!at_renderer_begin_frame(renderer)) {
                continue;
            }

            uint32_t width  = 0;
            uint32_t height = 0;
            at_renderer_get_size(renderer, &width, &height);

            /* Scene constants: once per frame */
            scene_uniforms scene;
            scene.view_projection    = at_camera3d_view_projection(&camera, (float)width,
                                                                   (float)height);
            scene.camera_position[0] = camera.position.x;
            scene.camera_position[1] = camera.position.y;
            scene.camera_position[2] = camera.position.z;
            scene.camera_position[3] = 1.0f;
            scene.fog_color[0]       = sky.r;
            scene.fog_color[1]       = sky.g;
            scene.fog_color[2]       = sky.b;
            scene.fog_color[3]       = 0.012f; /* Density */

            at_vec3 light = at_vec3_normalize(at_vec3_make(-0.4f, -1.0f, -0.3f));
            scene.light_direction[0] = light.x;
            scene.light_direction[1] = light.y;
            scene.light_direction[2] = light.z;
            scene.light_direction[3] = 0.0f;

            at_buffer_update(renderer, scene_buffer, &scene, sizeof(scene), 0);

            /* 3D: the boxes */
            at_renderer_bind_pipeline(renderer, depth_test ? depth_pipeline : no_depth_pipeline);
            at_renderer_bind_uniform_buffer(renderer, 0, scene_buffer);
            at_renderer_bind_vertex_buffer(renderer, vertex_buffer);
            at_renderer_bind_index_buffer(renderer, index_buffer);
            draw_scene(renderer, (float)now);

            /* 2D on top: the 2D renderer doesn't test depth */
            at_renderer2d_begin(r2d, &hud_camera);
            draw_hud(r2d, font, (float)width, (float)height, &camera, &fly, depth_test, fps);
            at_renderer2d_end(r2d);

            at_renderer_end_frame(renderer);
        }
    }

    /* Destroy in reverse order of creation */
    at_window_set_mouse_captured(window, false);
    at_font_destroy(font, renderer);
    at_renderer2d_destroy(r2d);
    at_pipeline_destroy(renderer, no_depth_pipeline);
    at_pipeline_destroy(renderer, depth_pipeline);
    at_buffer_destroy(renderer, scene_buffer);
    at_buffer_destroy(renderer, index_buffer);
    at_buffer_destroy(renderer, vertex_buffer);
    at_renderer_destroy(renderer);
    at_window_destroy(window);
    at_platform_shutdown();
    return 0;
}