/*
 * 2D shapes example: rectangles, circles, triangles and lines drawn with the
 * 2D renderer, all in a few draw calls.
 *   Left mouse button (drag): move the camera
 *   Mouse wheel:              zoom toward the cursor
 *   Right mouse button:       reset the camera
 *
 * Zoom into the big circle: it gets more segments and stays round. The grid
 * lines are always 1 pixel wide, whatever the zoom.
 */

#include <math.h>
#include <stdio.h>

#include <archtype/archtype.h>

#define WINDOW_TITLE "Archtype - Shapes 2D"

#define GRID_EXTENT  640.0f /* The grid covers -GRID_EXTENT..GRID_EXTENT */
#define GRID_STEP    64.0f

#define ZOOM_STEP 1.1f
#define MIN_ZOOM  0.1f
#define MAX_ZOOM  20.0f

/* ------------------------------------------------------------------------ */
/* Camera controls (same as the camera2d example)                            */
/* ------------------------------------------------------------------------ */

typedef struct camera_controls {
    bool    dragging;
    at_vec2 last_mouse;
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

    float wheel = at_window_get_mouse_wheel(window);
    if (wheel != 0.0f) {
        float target = at_clampf(camera->zoom * powf(ZOOM_STEP, wheel), MIN_ZOOM, MAX_ZOOM);
        at_camera2d_zoom_at(camera, mouse, target / camera->zoom,
                            viewport_width, viewport_height);
    }
}

/* ------------------------------------------------------------------------ */
/* Scene                                                                     */
/* ------------------------------------------------------------------------ */

static void draw_grid(at_renderer2d *r2d, const at_camera2d *camera) {
    float    pixel = 1.0f / camera->zoom; /* One pixel on screen, in world units */
    at_color line  = at_color_rgb(0.18f, 0.18f, 0.22f);

    for (float x = -GRID_EXTENT; x <= GRID_EXTENT; x += GRID_STEP) {
        at_renderer2d_draw_line(r2d, at_vec2_make(x, -GRID_EXTENT), at_vec2_make(x, GRID_EXTENT),
                                pixel, line);
    }
    for (float y = -GRID_EXTENT; y <= GRID_EXTENT; y += GRID_STEP) {
        at_renderer2d_draw_line(r2d, at_vec2_make(-GRID_EXTENT, y), at_vec2_make(GRID_EXTENT, y),
                                pixel, line);
    }

    /* Axes: X red, Y green */
    at_renderer2d_draw_line(r2d, at_vec2_make(-GRID_EXTENT, 0.0f), at_vec2_make(GRID_EXTENT, 0.0f),
                            2.0f * pixel, at_color_hex(0xE04848FF));
    at_renderer2d_draw_line(r2d, at_vec2_make(0.0f, -GRID_EXTENT), at_vec2_make(0.0f, GRID_EXTENT),
                            2.0f * pixel, at_color_hex(0x48E048FF));
}

static void draw_shapes(at_renderer2d *r2d, float time) {
    /* Rectangles: a row turning at different speeds */
    for (int i = 0; i < 5; ++i) {
        float    x     = -384.0f + (float)i * 96.0f;
        float    hue   = (float)i / 5.0f;
        at_color color = at_color_rgb(0.5f + 0.5f * cosf(AT_TAU * hue),
                                      0.5f + 0.5f * cosf(AT_TAU * (hue + 0.33f)),
                                      0.5f + 0.5f * cosf(AT_TAU * (hue + 0.66f)));
        at_renderer2d_draw_rect(r2d, at_vec2_make(x, 320.0f), at_vec2_make(64.0f, 40.0f),
                                time * (0.5f + 0.3f * (float)i), color);
    }

    /* Circles: small, medium and large (zoom in to see the segments) */
    at_renderer2d_draw_circle(r2d, at_vec2_make(-320.0f, 96.0f), 8.0f, at_color_hex(0xF2C14EFF));
    at_renderer2d_draw_circle(r2d, at_vec2_make(-224.0f, 96.0f), 32.0f, at_color_hex(0xF78154FF));
    at_renderer2d_draw_circle(r2d, at_vec2_make(0.0f, 96.0f), 128.0f, at_color_hex(0x4D9DE0FF));

    /* Half-transparent circles: alpha blending, later shapes on top */
    float pulse = 0.5f + 0.5f * sinf(time * 2.0f);
    at_renderer2d_draw_circle(r2d, at_vec2_make(256.0f, 64.0f), 80.0f,
                              at_color_rgba(1.0f, 0.2f, 0.3f, 0.6f));
    at_renderer2d_draw_circle(r2d, at_vec2_make(320.0f, 128.0f), 80.0f,
                              at_color_rgba(0.2f, 1.0f, 0.4f, 0.6f));
    at_renderer2d_draw_circle(r2d, at_vec2_make(352.0f, 48.0f), 80.0f * (0.6f + 0.4f * pulse),
                              at_color_rgba(0.3f, 0.4f, 1.0f, 0.6f));

    /* Triangles */
    at_renderer2d_draw_triangle(r2d, at_vec2_make(-448.0f, -320.0f), at_vec2_make(-320.0f, -320.0f),
                                at_vec2_make(-384.0f, -192.0f), at_color_hex(0x7DCE82FF));
    at_renderer2d_draw_triangle(r2d, at_vec2_make(-256.0f, -192.0f), at_vec2_make(-128.0f, -192.0f),
                                at_vec2_make(-192.0f, -320.0f), at_color_hex(0xE15A97FF));

    /* Lines: a clock with hands of different thicknesses */
    at_vec2 clock = at_vec2_make(256.0f, -256.0f);
    at_renderer2d_draw_circle(r2d, clock, 104.0f, at_color_hex(0xE8E8E8FF));
    at_renderer2d_draw_circle(r2d, clock, 96.0f, at_color_hex(0x202028FF));

    for (int i = 0; i < 12; ++i) {
        float angle = AT_TAU * (float)i / 12.0f;
        at_vec2 dir = at_vec2_make(cosf(angle), sinf(angle));
        at_renderer2d_draw_line(r2d, at_vec2_add(clock, at_vec2_scale(dir, 76.0f)),
                                at_vec2_add(clock, at_vec2_scale(dir, 90.0f)),
                                3.0f, at_color_hex(0xE8E8E8FF));
    }

    /* Hands turn clockwise: the angle goes down over time */
    float minute = AT_PI * 0.5f - time * 0.6f;
    float hour   = AT_PI * 0.5f - time * 0.05f;
    at_renderer2d_draw_line(r2d, clock, at_vec2_add(clock, at_vec2_make(cosf(hour) * 48.0f,
                                                                        sinf(hour) * 48.0f)),
                            8.0f, at_color_hex(0xF2C14EFF));
    at_renderer2d_draw_line(r2d, clock, at_vec2_add(clock, at_vec2_make(cosf(minute) * 80.0f,
                                                                        sinf(minute) * 80.0f)),
                            4.0f, at_color_hex(0x4D9DE0FF));
    at_renderer2d_draw_circle(r2d, clock, 8.0f, at_color_hex(0xE8E8E8FF));
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
        .app_name          = "Shapes 2D Example",
        .enable_validation = true,
        .vsync             = true,
    };

    at_renderer *renderer = at_renderer_create(&renderer_desc);
    if (!renderer) {
        at_window_destroy(window);
        at_platform_shutdown();
        return 1;
    }

    at_renderer2d *r2d = at_renderer2d_create(renderer, NULL);

    if (r2d) {
        at_renderer_set_clear_color(renderer, 0.08f, 0.08f, 0.10f, 1.0f);

        at_camera2d     camera      = at_camera2d_make();
        camera_controls controls    = { 0 };
        double          last_time   = at_platform_get_time();
        double          fps_timer   = 0.0;
        uint32_t        frame_count = 0;

        while (!at_window_should_close(window)) {
            at_platform_poll_events();

            if (at_window_is_minimized(window)) {
                at_platform_wait_events();
                controls.dragging = false;
                last_time         = at_platform_get_time();
                continue;
            }

            double now = at_platform_get_time();
            fps_timer += now - last_time;
            last_time  = now;

            if (!at_renderer_begin_frame(renderer)) {
                continue;
            }

            uint32_t width  = 0;
            uint32_t height = 0;
            at_renderer_get_size(renderer, &width, &height);
            update_camera(window, &camera, &controls, (float)width, (float)height);

            at_renderer2d_begin(r2d, &camera);
            draw_grid(r2d, &camera);
            draw_shapes(r2d, (float)now);
            at_renderer2d_end(r2d);

            at_renderer_end_frame(renderer);

            /* FPS and zoom in the title once per second */
            frame_count++;
            if (fps_timer >= 1.0) {
                char title[160];
                snprintf(title, sizeof(title), WINDOW_TITLE " | %u FPS | zoom %.2fx",
                         (unsigned)frame_count, (double)camera.zoom);
                at_window_set_title(window, title);
                fps_timer   = 0.0;
                frame_count = 0;
            }
        }
    }

    /* Destroy in reverse order of creation */
    at_renderer2d_destroy(r2d);
    at_renderer_destroy(renderer);
    at_window_destroy(window);
    at_platform_shutdown();
    return 0;
}