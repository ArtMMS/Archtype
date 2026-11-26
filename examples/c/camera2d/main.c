/*
 * 2D camera example: a grid of tiles seen through an orthographic camera.
 *   Left mouse button (drag): move the camera
 *   Mouse wheel:              zoom toward the cursor
 *   Right mouse button:       reset the camera
 *
 * World space is in pixels at zoom 1, with +Y up: tiles get greener going
 * up and redder going right. The red and green bars are the X and Y axes.
 */

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include <archtype/archtype.h>

#define WINDOW_TITLE "Archtype - Camera 2D"

#define GRID_SIZE         16     /* Tiles per side                      */
#define TILE_SIZE         64.0f  /* World units (pixels at zoom 1)      */
#define TILE_GAP          4.0f   /* Space between tiles                 */
#define AXIS_WIDTH        4.0f
#define QUAD_COUNT        (GRID_SIZE * GRID_SIZE + 2) /* Tiles + 2 axes */
#define VERTICES_PER_QUAD 6

#define ZOOM_STEP 1.1f   /* Zoom change per wheel notch */
#define MIN_ZOOM  0.1f
#define MAX_ZOOM  10.0f

/* Must match the inputs of camera2d.vert */
typedef struct vertex {
    float position[2];
    float color[3];
} vertex;

/* Must match "uniform Camera" in camera2d.vert */
typedef struct camera_uniforms {
    at_mat4 view_projection;
} camera_uniforms;

/* Static: too big for the stack */
static vertex s_vertices[QUAD_COUNT * VERTICES_PER_QUAD];

/* ------------------------------------------------------------------------ */
/* Scene                                                                     */
/* ------------------------------------------------------------------------ */

/* Two triangles covering [x0, x1] x [y0, y1], counter-clockwise with +Y up */
static vertex *add_quad(vertex *out, float x0, float y0, float x1, float y1,
                        float r, float g, float b) {
    const float corners[VERTICES_PER_QUAD][2] = {
        { x0, y0 }, { x1, y0 }, { x1, y1 },
        { x1, y1 }, { x0, y1 }, { x0, y0 },
    };

    for (int i = 0; i < VERTICES_PER_QUAD; ++i) {
        out[i].position[0] = corners[i][0];
        out[i].position[1] = corners[i][1];
        out[i].color[0]    = r;
        out[i].color[1]    = g;
        out[i].color[2]    = b;
    }
    return out + VERTICES_PER_QUAD;
}

static void build_scene(void) {
    vertex *out    = s_vertices;
    float   extent = GRID_SIZE * TILE_SIZE * 0.5f; /* The grid is centered at the origin */

    for (int row = 0; row < GRID_SIZE; ++row) {
        for (int column = 0; column < GRID_SIZE; ++column) {
            float x0 = -extent + (float)column * TILE_SIZE + TILE_GAP * 0.5f;
            float y0 = -extent + (float)row * TILE_SIZE + TILE_GAP * 0.5f;
            float x1 = x0 + TILE_SIZE - TILE_GAP;
            float y1 = y0 + TILE_SIZE - TILE_GAP;

            /* Red grows to the right, green grows upward */
            float shade = ((row + column) % 2 == 0) ? 1.0f : 0.75f;
            float r     = (float)column / (GRID_SIZE - 1) * shade;
            float g     = (float)row / (GRID_SIZE - 1) * shade;
            float b     = 0.35f * shade;

            out = add_quad(out, x0, y0, x1, y1, r, g, b);
        }
    }

    /* Axes through the origin, drawn last so they stay on top */
    out = add_quad(out, -extent, -AXIS_WIDTH * 0.5f, extent, AXIS_WIDTH * 0.5f,
                   1.0f, 0.2f, 0.2f); /* X */
    add_quad(out, -AXIS_WIDTH * 0.5f, -extent, AXIS_WIDTH * 0.5f, extent,
             0.2f, 1.0f, 0.2f);       /* Y */
}

/* ------------------------------------------------------------------------ */
/* Input                                                                     */
/* ------------------------------------------------------------------------ */

typedef struct camera_controls {
    bool    dragging;
    at_vec2 last_mouse; /* Screen position at the previous frame of the drag */
} camera_controls;

static void update_camera(at_window *window, at_camera2d *camera, camera_controls *controls,
                          float viewport_width, float viewport_height) {
    at_vec2 mouse;
    at_window_get_mouse_position(window, &mouse.x, &mouse.y);

    if (at_window_is_mouse_button_down(window, AT_MOUSE_BUTTON_RIGHT)) {
        *camera            = at_camera2d_make();
        controls->dragging = false;
        return;
    }

    /* Drag: move the camera by how much the world moved under the mouse, */
    /* so the point that was grabbed stays under the cursor.              */
    if (at_window_is_mouse_button_down(window, AT_MOUSE_BUTTON_LEFT)) {
        if (controls->dragging) {
            at_vec2 from = at_camera2d_screen_to_world(camera, controls->last_mouse,
                                                       viewport_width, viewport_height);
            at_vec2 to   = at_camera2d_screen_to_world(camera, mouse,
                                                       viewport_width, viewport_height);
            camera->position = at_vec2_add(camera->position, at_vec2_sub(from, to));
        }
        controls->dragging   = true;
        controls->last_mouse = mouse;
    } else {
        controls->dragging = false;
    }

    /* Wheel: zoom toward the cursor, within limits */
    float wheel = at_window_get_mouse_wheel(window);
    if (wheel != 0.0f) {
        float target = at_clampf(camera->zoom * powf(ZOOM_STEP, wheel), MIN_ZOOM, MAX_ZOOM);
        at_camera2d_zoom_at(camera, mouse, target / camera->zoom,
                            viewport_width, viewport_height);
    }
}

/* Shows the zoom and the world position under the mouse in the title */
static void update_title(at_window *window, const at_camera2d *camera,
                         float viewport_width, float viewport_height) {
    at_vec2 mouse;
    at_window_get_mouse_position(window, &mouse.x, &mouse.y);
    at_vec2 world = at_camera2d_screen_to_world(camera, mouse, viewport_width, viewport_height);

    char title[160];
    snprintf(title, sizeof(title), WINDOW_TITLE " | zoom %.2fx | mouse at (%.0f, %.0f)",
             (double)camera->zoom, (double)world.x, (double)world.y);
    at_window_set_title(window, title);
}

/* ------------------------------------------------------------------------ */
/* Main                                                                      */
/* ------------------------------------------------------------------------ */

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
        .app_name          = "Camera 2D Example",
        .enable_validation = true,
        .vsync             = true,
    };

    at_renderer *renderer = at_renderer_create(&renderer_desc);
    if (!renderer) {
        at_window_destroy(window);
        at_platform_shutdown();
        return 1;
    }

    /* Buffers: the scene never changes, the camera changes every frame */
    build_scene();

    at_buffer_desc vertex_desc = {
        .type = AT_BUFFER_VERTEX,
        .size = sizeof(s_vertices),
        .data = s_vertices,
    };
    at_buffer_desc camera_desc = {
        .type = AT_BUFFER_UNIFORM,
        .size = sizeof(camera_uniforms),
    };

    at_buffer *vertex_buffer = at_buffer_create(renderer, &vertex_desc);
    at_buffer *camera_buffer = at_buffer_create(renderer, &camera_desc);

    /* Pipeline */
    at_shader *vertex_shader   = at_shader_load(renderer, "shaders/camera2d.vert.spv");
    at_shader *fragment_shader = at_shader_load(renderer, "shaders/camera2d.frag.spv");

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
            .binding_count = 1,
            .bindings      = {
                { .binding = 0, .type = AT_BINDING_UNIFORM_BUFFER,
                  .stages = AT_SHADER_STAGE_VERTEX },
            },
            .cull_mode = AT_CULL_NONE, /* A rotated camera must not hide tiles */
        };
        pipeline = at_pipeline_create(renderer, &pipeline_desc);
    }

    at_shader_destroy(renderer, vertex_shader);
    at_shader_destroy(renderer, fragment_shader);

    if (pipeline && vertex_buffer && camera_buffer) {
        at_renderer_set_clear_color(renderer, 0.08f, 0.08f, 0.10f, 1.0f);

        at_camera2d     camera     = at_camera2d_make();
        camera_controls controls   = { 0 };
        double          title_time = 0.0;

        while (!at_window_should_close(window)) {
            at_platform_poll_events();

            if (at_window_is_minimized(window)) {
                at_platform_wait_events();
                controls.dragging = false;
                continue;
            }

            if (!at_renderer_begin_frame(renderer)) {
                continue;
            }

            /* The size of the images being drawn: projection and mouse */
            /* coordinates both use it.                                 */
            uint32_t width  = 0;
            uint32_t height = 0;
            at_renderer_get_size(renderer, &width, &height);

            update_camera(window, &camera, &controls, (float)width, (float)height);

            /* Changing the title every frame is slow on some systems */
            double now = at_platform_get_time();
            if (now - title_time >= 0.1) {
                update_title(window, &camera, (float)width, (float)height);
                title_time = now;
            }

            camera_uniforms uniforms = {
                at_camera2d_view_projection(&camera, (float)width, (float)height)
            };
            at_buffer_update(renderer, camera_buffer, &uniforms, sizeof(uniforms), 0);

            at_renderer_bind_pipeline(renderer, pipeline);
            at_renderer_bind_uniform_buffer(renderer, 0, camera_buffer);
            at_renderer_bind_vertex_buffer(renderer, vertex_buffer);
            at_renderer_draw(renderer, QUAD_COUNT * VERTICES_PER_QUAD);

            at_renderer_end_frame(renderer);
        }
    }

    /* Destroy in reverse order of creation */
    at_pipeline_destroy(renderer, pipeline);
    at_buffer_destroy(renderer, camera_buffer);
    at_buffer_destroy(renderer, vertex_buffer);
    at_renderer_destroy(renderer);
    at_window_destroy(window);
    at_platform_shutdown();
    return 0;
}