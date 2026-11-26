/*
 * 2D text example: the engine's built-in bitmap font.
 *   Left mouse button (drag): move the camera
 *   Mouse wheel:              zoom toward the cursor (the pixels stay sharp)
 *   Right mouse button:       reset the camera
 *
 * Shows every character, scales, colors, alignment, a box sized with
 * at_font_measure, a typewriter effect, and a HUD with the frame's stats
 * drawn in a second begin/end with a fixed camera.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <archtype/archtype.h>

#define WINDOW_TITLE "Archtype - Text 2D"

#define ZOOM_STEP 1.1f
#define MIN_ZOOM  0.25f
#define MAX_ZOOM  16.0f

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

/* Text with a dark shadow one texel down and to the right */
static void draw_shadowed(at_renderer2d *r2d, const at_font *font, const char *text,
                          at_vec2 position, float scale, at_color color) {
    at_renderer2d_set_layer(r2d, 0);
    at_renderer2d_draw_text(r2d, font, text, at_vec2_make(position.x + scale, position.y - scale),
                            scale, at_color_rgba(0.0f, 0.0f, 0.0f, 0.6f));
    at_renderer2d_set_layer(r2d, 1);
    at_renderer2d_draw_text(r2d, font, text, position, scale, color);
}

/* Every character of the font, 32 per line */
static void draw_charset(at_renderer2d *r2d, const at_font *font, at_vec2 position) {
    char text[3 * 33 + 1];
    int  length = 0;

    for (int c = 32; c < 127; ++c) {
        text[length++] = (char)c;
        if ((c - 31) % 32 == 0) {
            text[length++] = '\n';
        }
    }
    text[length] = '\0';

    at_renderer2d_draw_text(r2d, font, text, position, 2.0f, at_color_hex(0xD8D8E0FF));
}

/* Three texts aligned to the same vertical guide line */
static void draw_alignment(at_renderer2d *r2d, const at_font *font, at_vec2 guide) {
    at_renderer2d_draw_line(r2d, at_vec2_make(guide.x, guide.y + 8.0f),
                            at_vec2_make(guide.x, guide.y - 100.0f), 1.0f,
                            at_color_hex(0xE04848FF));

    const at_text_align aligns[3] = { AT_TEXT_ALIGN_LEFT, AT_TEXT_ALIGN_CENTER, AT_TEXT_ALIGN_RIGHT };
    const char *const   names[3]  = { "Left aligned", "Centered", "Right aligned" };

    for (int i = 0; i < 3; ++i) {
        at_text_desc text = at_text_desc_make(font, names[i],
                                              at_vec2_make(guide.x, guide.y - (float)i * 32.0f));
        text.scale = 2.0f;
        text.align = aligns[i];
        at_renderer2d_draw_text_ex(r2d, &text);
    }
}

/* A paragraph in a box that fits it, from at_font_measure */
static void draw_paragraph(at_renderer2d *r2d, const at_font *font, at_vec2 top_left) {
    const char *text    = "Bitmap fonts are images with the\n"
                          "characters in a grid. Every letter\n"
                          "is a sprite of the same texture, so\n"
                          "all of this is a single draw call.";
    const float scale   = 2.0f;
    const float padding = 10.0f;

    at_vec2 size = at_font_measure(font, text, scale);
    at_vec2 box  = at_vec2_make(size.x + 2.0f * padding, size.y + 2.0f * padding);

    at_renderer2d_set_layer(r2d, 0);
    at_renderer2d_draw_rect(r2d, at_vec2_make(top_left.x + box.x * 0.5f, top_left.y - box.y * 0.5f),
                            box, 0.0f, at_color_hex(0x262636FF));

    at_renderer2d_set_layer(r2d, 1);
    at_renderer2d_draw_text(r2d, font, text,
                            at_vec2_make(top_left.x + padding, top_left.y - padding),
                            scale, at_color_hex(0xF2C14EFF));
}

/* The message appears one character at a time, with a blinking cursor */
static void draw_typewriter(at_renderer2d *r2d, const at_font *font, at_vec2 position,
                            float time) {
    const char *message = "Hello from Archtype! (C99 + Vulkan)";
    size_t      length  = strlen(message);
    size_t      shown   = (size_t)fmodf(time * 12.0f, (float)length + 24.0f); /* Pause at the end */
    if (shown > length) {
        shown = length;
    }

    char text[64];
    memcpy(text, message, shown);
    text[shown] = '\0';
    if (fmodf(time, 1.0f) < 0.5f) {
        text[shown]     = '_';
        text[shown + 1] = '\0';
    }

    draw_shadowed(r2d, font, text, position, 3.0f, at_color_hex(0x7DCE82FF));
}

/* The same font at scales 1 to 4, and a few colors */
static void draw_scales(at_renderer2d *r2d, const at_font *font, at_vec2 position) {
    float y = position.y;
    for (int scale = 1; scale <= 4; ++scale) {
        char text[32];
        snprintf(text, sizeof(text), "Scale %d", scale);
        at_renderer2d_draw_text(r2d, font, text, at_vec2_make(position.x, y), (float)scale,
                                at_color_white());
        y -= at_font_get_line_height(font) * (float)scale + 4.0f;
    }

    const at_color colors[5] = {
        at_color_hex(0xE04848FF), at_color_hex(0xF2C14EFF), at_color_hex(0x7DCE82FF),
        at_color_hex(0x4D9DE0FF), at_color_hex(0xE15A97FF),
    };
    const char *const words[5] = { "red", "yellow", "green", "blue", "pink" };

    float x = position.x;
    for (int i = 0; i < 5; ++i) {
        at_renderer2d_draw_text(r2d, font, words[i], at_vec2_make(x, y), 2.0f, colors[i]);
        x += at_font_measure(font, words[i], 2.0f).x + 12.0f;
    }
}

/* HUD in screen pixels (default camera: origin at the center, +Y up) */
static void draw_hud(at_renderer2d *r2d, const at_font *font, float width, float height,
                     const char *line) {
    const float margin  = 8.0f; /* From the screen corner */
    const float padding = 8.0f; /* Inside the box         */

    at_vec2 size = at_font_measure(font, line, 2.0f);
    at_vec2 box  = at_vec2_make(size.x + 2.0f * padding, size.y + 2.0f * padding);

    /* Box in the bottom-left corner; the text starts at its top-left, inside */
    at_renderer2d_draw_rect(r2d, at_vec2_make(-width * 0.5f + margin + box.x * 0.5f,
                                              -height * 0.5f + margin + box.y * 0.5f),
                            box, 0.0f, at_color_rgba(0.0f, 0.0f, 0.0f, 0.6f));
    at_renderer2d_draw_text(r2d, font, line,
                            at_vec2_make(-width * 0.5f + margin + padding,
                                         -height * 0.5f + margin + box.y - padding),
                            2.0f, at_color_white());
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
        .app_name          = "Text 2D Example",
        .enable_validation = true,
        .vsync             = true,
    };

    at_renderer *renderer = at_renderer_create(&renderer_desc);
    if (!renderer) {
        at_window_destroy(window);
        at_platform_shutdown();
        return 1;
    }

    at_renderer2d *r2d  = at_renderer2d_create(renderer, NULL);
    at_font       *font = at_font_create_default(renderer);

    if (r2d && font) {
        at_renderer_set_clear_color(renderer, 0.08f, 0.08f, 0.10f, 1.0f);

        at_camera2d     camera     = at_camera2d_make();
        at_camera2d     hud_camera = at_camera2d_make();
        camera_controls controls   = { 0 };
        double          last_time  = at_platform_get_time();
        double          fps_timer  = 0.0;
        uint32_t        frames     = 0;
        uint32_t        fps        = 0;

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
            frames++;
            if (fps_timer >= 1.0) {
                fps       = frames;
                frames    = 0;
                fps_timer = 0.0;
            }

            if (!at_renderer_begin_frame(renderer)) {
                continue;
            }

            uint32_t width  = 0;
            uint32_t height = 0;
            at_renderer_get_size(renderer, &width, &height);
            update_camera(window, &camera, &controls, (float)width, (float)height);

            float time = (float)now;

            /* World */
            at_renderer2d_begin(r2d, &camera);
            draw_shadowed(r2d, font, "Archtype text", at_vec2_make(-600.0f, 340.0f), 4.0f,
                          at_color_hex(0x4D9DE0FF));
            draw_charset(r2d, font, at_vec2_make(-600.0f, 280.0f));
            draw_paragraph(r2d, font, at_vec2_make(-600.0f, 200.0f));
            draw_alignment(r2d, font, at_vec2_make(400.0f, 200.0f));
            draw_typewriter(r2d, font, at_vec2_make(-600.0f, 60.0f), time);
            draw_scales(r2d, font, at_vec2_make(-600.0f, -20.0f));
            at_renderer2d_end(r2d);

            /* Stats of the world pass, then the HUD on top with a fixed camera */
            at_renderer2d_stats stats = at_renderer2d_get_stats(r2d);

            at_vec2 mouse;
            at_window_get_mouse_position(window, &mouse.x, &mouse.y);
            at_vec2 world = at_camera2d_screen_to_world(&camera, mouse, (float)width,
                                                        (float)height);

            char hud[160];
            snprintf(hud, sizeof(hud),
                     "%u FPS   %u draw calls   %u sprites   zoom %.2fx\n"
                     "Mouse at (%.0f, %.0f)",
                     (unsigned)fps, (unsigned)stats.draw_calls, (unsigned)stats.primitives,
                     (double)camera.zoom, (double)world.x, (double)world.y);

            at_renderer2d_begin(r2d, &hud_camera);
            draw_hud(r2d, font, (float)width, (float)height, hud);
            at_renderer2d_end(r2d);

            at_renderer_end_frame(renderer);
        }
    }

    /* Destroy in reverse order of creation */
    at_font_destroy(font, renderer);
    at_renderer2d_destroy(r2d);
    at_renderer_destroy(renderer);
    at_window_destroy(window);
    at_platform_shutdown();
    return 0;
}