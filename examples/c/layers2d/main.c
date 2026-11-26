/*
 * 2D layers example: draw order with layers, and blend modes.
 *   Left mouse button (drag): move the camera
 *   Mouse wheel:              zoom toward the cursor
 *   Right mouse button:       reset the camera
 *
 * Left:   three see-through cards drawn always in the same order; their
 *         layers change every second, so a different one is on top.
 * Middle: slimes walking on the ground seen from above. Each slime and its
 *         shadow are drawn one after the other, but the shadows go on a
 *         low layer and each slime's layer comes from its Y position: the
 *         ones lower on the screen are in front.
 * Right:  the same glows with alpha blending (top) and additive blending
 *         (bottom), where overlapping glows add up to white.
 * Top:    a HUD bar drawn in a second begin/end, always on top.
 */

#include <math.h>
#include <stdio.h>

#include <archtype/archtype.h>

#define WINDOW_TITLE "Archtype - Layers 2D"

#define SLIME_SIZE   16
#define SLIME_SCALE  4.0f
#define GLOW_SIZE    64
#define SLIME_COUNT  12
#define GLOW_COUNT   7

/* Layers of the scene */
#define LAYER_GROUND  -1000000 /* Below everything in the world    */
#define LAYER_SHADOWS -999999  /* Shadows lie on the ground        */

#define ZOOM_STEP 1.1f
#define MIN_ZOOM  0.1f
#define MAX_ZOOM  20.0f

/* ------------------------------------------------------------------------ */
/* Textures made in code (same as the sprites2d example)                     */
/* ------------------------------------------------------------------------ */

static const char *const k_slime_rows[SLIME_SIZE] = {
    "................",
    "................",
    "................",
    "................",
    "................",
    "......KKKK......",
    "....KKggggKK....",
    "...KgggggLLgK...",
    "..KggggggggLgK..",
    "..KggWKggWKggK..",
    ".KgggWKggWKgggK.",
    ".KggggggggggggK.",
    ".KgggggKKgggggK.",
    ".KddggggggggddK.",
    ".KddddddddddddK.",
    "..KKKKKKKKKKKK..",
};

static void slime_color(char c, uint8_t out[4]) {
    uint32_t rgba;
    switch (c) {
        case 'K': rgba = 0x1B1B2AFF; break;
        case 'g': rgba = 0x5BC05BFF; break;
        case 'd': rgba = 0x3A8A3AFF; break;
        case 'L': rgba = 0xB4F0B4FF; break;
        case 'W': rgba = 0xFFFFFFFF; break;
        default:  rgba = 0x00000000; break;
    }
    out[0] = (uint8_t)(rgba >> 24);
    out[1] = (uint8_t)(rgba >> 16);
    out[2] = (uint8_t)(rgba >> 8);
    out[3] = (uint8_t)rgba;
}

static at_texture *create_slime_texture(at_renderer *renderer) {
    uint8_t pixels[SLIME_SIZE * SLIME_SIZE * 4];

    for (int y = 0; y < SLIME_SIZE; ++y) {
        for (int x = 0; x < SLIME_SIZE; ++x) {
            slime_color(k_slime_rows[y][x], &pixels[(y * SLIME_SIZE + x) * 4]);
        }
    }

    at_texture_options options = {
        .filter = AT_TEXTURE_FILTER_NEAREST,
        .wrap   = AT_TEXTURE_WRAP_CLAMP,
    };
    return at_texture_create(renderer, SLIME_SIZE, SLIME_SIZE, pixels, &options);
}

static at_texture *create_glow_texture(at_renderer *renderer) {
    static uint8_t pixels[GLOW_SIZE * GLOW_SIZE * 4];

    for (int y = 0; y < GLOW_SIZE; ++y) {
        for (int x = 0; x < GLOW_SIZE; ++x) {
            float dx = ((float)x + 0.5f) / GLOW_SIZE * 2.0f - 1.0f;
            float dy = ((float)y + 0.5f) / GLOW_SIZE * 2.0f - 1.0f;
            float d  = at_clampf(sqrtf(dx * dx + dy * dy), 0.0f, 1.0f);
            float a  = (1.0f - d) * (1.0f - d);

            uint8_t *p = &pixels[(y * GLOW_SIZE + x) * 4];
            p[0] = 255;
            p[1] = 255;
            p[2] = 255;
            p[3] = (uint8_t)(a * 255.0f + 0.5f);
        }
    }

    at_texture_options options = { .wrap = AT_TEXTURE_WRAP_CLAMP };
    return at_texture_create(renderer, GLOW_SIZE, GLOW_SIZE, pixels, &options);
}

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

/* Three see-through cards, always drawn red, green, blue. Their layers */
/* rotate every second, so the one on top changes.                      */
static void draw_cards(at_renderer2d *r2d, float time, int *front) {
    const at_color colors[3] = {
        at_color_rgba(0.95f, 0.30f, 0.30f, 0.85f),
        at_color_rgba(0.30f, 0.90f, 0.40f, 0.85f),
        at_color_rgba(0.30f, 0.50f, 1.00f, 0.85f),
    };
    const at_vec2 centers[3] = {
        at_vec2_make(-520.0f,  60.0f),
        at_vec2_make(-460.0f,   0.0f),
        at_vec2_make(-400.0f, -60.0f),
    };

    int step = (int)time;
    for (int i = 0; i < 3; ++i) {
        int layer = (i + step) % 3;
        if (layer == 2) {
            *front = i;
        }

        at_renderer2d_set_layer(r2d, layer);
        at_renderer2d_draw_rect(r2d, centers[i], at_vec2_make(150.0f, 200.0f), 0.0f, colors[i]);
    }
}

/* Slimes wandering in a field seen from above. Drawn in index order, but */
/* sorted by their feet: lower on the screen = higher layer = in front.   */
static void draw_field(at_renderer2d *r2d, at_texture *slime, float time) {
    const at_vec2 center = at_vec2_make(0.0f, -40.0f);
    const float   size   = SLIME_SIZE * SLIME_SCALE;

    /* The ground, below everything */
    at_renderer2d_set_layer(r2d, LAYER_GROUND);
    at_renderer2d_draw_rect(r2d, center, at_vec2_make(440.0f, 340.0f), 0.0f,
                            at_color_hex(0x2C3A2CFF));

    for (int i = 0; i < SLIME_COUNT; ++i) {
        float a = 0.35f + 0.07f * (float)i;
        float b = 0.50f + 0.05f * (float)i;
        float p = (float)i * 2.1f;

        at_vec2 feet = at_vec2_make(center.x + sinf(time * a + p) * 170.0f,
                                    center.y + cosf(time * b + p) * 120.0f - 30.0f);
        bool facing_left = cosf(time * a + p) < 0.0f; /* x is going down */

        /* Shadow on its own low layer, right before the slime */
        at_renderer2d_set_layer(r2d, LAYER_SHADOWS);
        at_renderer2d_draw_rect(r2d, at_vec2_make(feet.x, feet.y + 2.0f),
                                at_vec2_make(44.0f, 8.0f), 0.0f,
                                at_color_rgba(0.0f, 0.0f, 0.0f, 0.35f));

        /* Lower Y (closer to the bottom of the screen) = higher layer */
        at_renderer2d_set_layer(r2d, -(int32_t)floorf(feet.y));

        at_sprite_desc sprite = at_sprite_desc_make(slime, feet);
        sprite.size   = at_vec2_make(size, size);
        sprite.origin = at_vec2_make(0.5f, 0.0f);
        sprite.flip_x = facing_left;
        at_renderer2d_draw_sprite_ex(r2d, &sprite);
    }
}

/* The same overlapping glows with alpha (top) and additive (bottom) blending */
static void draw_glows(at_renderer2d *r2d, at_texture *glow, float time) {
    at_renderer2d_set_layer(r2d, 0);

    /* Dark backdrop so the difference is easy to see */
    at_renderer2d_draw_rect(r2d, at_vec2_make(480.0f, 0.0f), at_vec2_make(300.0f, 420.0f), 0.0f,
                            at_color_hex(0x0C0C12FF));

    for (int row = 0; row < 2; ++row) {
        at_renderer2d_set_blend_mode(r2d, row == 0 ? AT_BLEND_ALPHA : AT_BLEND_ADDITIVE);
        float y = row == 0 ? 100.0f : -100.0f;

        for (int i = 0; i < GLOW_COUNT; ++i) {
            float    hue  = (float)i / GLOW_COUNT;
            float    x    = 400.0f + (float)i * 27.0f + sinf(time + (float)i) * 10.0f;
            at_color tint = at_color_rgba(0.5f + 0.5f * cosf(AT_TAU * hue),
                                          0.5f + 0.5f * cosf(AT_TAU * (hue + 0.33f)),
                                          0.5f + 0.5f * cosf(AT_TAU * (hue + 0.66f)), 0.9f);
            at_renderer2d_draw_sprite(r2d, glow, at_vec2_make(x, y), at_vec2_make(120.0f, 120.0f),
                                      0.0f, tint);
        }
    }

    at_renderer2d_set_blend_mode(r2d, AT_BLEND_ALPHA);
}

/* HUD in screen pixels: with the default camera, the origin is the screen */
/* center and +Y is up, so the top edge is at height / 2.                  */
static void draw_hud(at_renderer2d *r2d, float width, float height, int front) {
    const at_color colors[3] = {
        at_color_hex(0xF24D4DFF), at_color_hex(0x4DE666FF), at_color_hex(0x4D80FFFF),
    };

    float top = height * 0.5f;
    at_renderer2d_draw_rect(r2d, at_vec2_make(0.0f, top - 24.0f), at_vec2_make(width, 48.0f),
                            0.0f, at_color_rgba(0.0f, 0.0f, 0.0f, 0.6f));

    /* Which card is on top now */
    float left = -width * 0.5f;
    at_renderer2d_draw_rect(r2d, at_vec2_make(left + 32.0f, top - 24.0f),
                            at_vec2_make(28.0f, 28.0f), AT_PI * 0.25f, colors[front]);
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
        .app_name          = "Layers 2D Example",
        .enable_validation = true,
        .vsync             = true,
    };

    at_renderer *renderer = at_renderer_create(&renderer_desc);
    if (!renderer) {
        at_window_destroy(window);
        at_platform_shutdown();
        return 1;
    }

    at_renderer2d *r2d   = at_renderer2d_create(renderer, NULL);
    at_texture    *slime = create_slime_texture(renderer);
    at_texture    *glow  = create_glow_texture(renderer);

    if (r2d && slime && glow) {
        at_renderer_set_clear_color(renderer, 0.08f, 0.08f, 0.10f, 1.0f);

        at_camera2d     camera      = at_camera2d_make();
        at_camera2d     hud_camera  = at_camera2d_make(); /* Never moves */
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

            /* World */
            int front = 0;
            at_renderer2d_begin(r2d, &camera);
            draw_cards(r2d, (float)now, &front);
            draw_field(r2d, slime, (float)now);
            draw_glows(r2d, glow, (float)now);
            at_renderer2d_end(r2d);

            /* HUD: a second begin/end is always drawn over the first */
            at_renderer2d_begin(r2d, &hud_camera);
            draw_hud(r2d, (float)width, (float)height, front);
            at_renderer2d_end(r2d);

            at_renderer_end_frame(renderer);

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
    at_texture_destroy(renderer, glow);
    at_texture_destroy(renderer, slime);
    at_renderer2d_destroy(r2d);
    at_renderer_destroy(renderer);
    at_window_destroy(window);
    at_platform_shutdown();
    return 0;
}