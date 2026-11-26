/*
 * 2D sprites example: textured sprites with rotation, origin, flipping and
 * tint, mixed with shapes in the same 2D renderer.
 *   Left mouse button (drag): move the camera
 *   Mouse wheel:              zoom toward the cursor
 *   Right mouse button:       reset the camera
 *
 * The textures are made in code: a 16x16 pixel-art slime (sharp pixels,
 * nearest filter) and a soft glow (smooth, linear filter). If
 * bin/textures/image.png exists (examples/c/texture/image.png), it spins in
 * the middle.
 */

#include <math.h>
#include <stdio.h>

#include <archtype/archtype.h>

#define WINDOW_TITLE "Archtype - Sprites 2D"

#define SLIME_SIZE   16  /* Texels per side            */
#define SLIME_SCALE  4.0f
#define GLOW_SIZE    64
#define GLOW_COUNT   24
#define WALKER_COUNT 5

#define ZOOM_STEP 1.1f
#define MIN_ZOOM  0.1f
#define MAX_ZOOM  20.0f

/* ------------------------------------------------------------------------ */
/* Textures made in code                                                     */
/* ------------------------------------------------------------------------ */

/* Rows from top to bottom, like the image in memory. The eyes look right. */
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
        case 'K': rgba = 0x1B1B2AFF; break; /* Outline   */
        case 'g': rgba = 0x5BC05BFF; break; /* Body      */
        case 'd': rgba = 0x3A8A3AFF; break; /* Shadow    */
        case 'L': rgba = 0xB4F0B4FF; break; /* Highlight */
        case 'W': rgba = 0xFFFFFFFF; break; /* Eyes      */
        default:  rgba = 0x00000000; break; /* Empty     */
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

    /* Nearest: keeps the pixel art sharp when scaled up */
    at_texture_options options = {
        .filter = AT_TEXTURE_FILTER_NEAREST,
        .wrap   = AT_TEXTURE_WRAP_CLAMP,
    };
    return at_texture_create(renderer, SLIME_SIZE, SLIME_SIZE, pixels, &options);
}

/* White circle that fades out toward the edge: the tint gives it a color */
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

    at_texture_options options = { .wrap = AT_TEXTURE_WRAP_CLAMP }; /* Linear filter */
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

typedef struct scene_textures {
    at_texture *slime;
    at_texture *glow;
    at_texture *image; /* May be NULL */
} scene_textures;

/* A ring of colored glows turning around the origin */
static void draw_glows(at_renderer2d *r2d, at_texture *glow, float time) {
    for (int i = 0; i < GLOW_COUNT; ++i) {
        float    hue   = (float)i / GLOW_COUNT;
        float    angle = AT_TAU * hue + time * 0.2f;
        at_vec2  where = at_vec2_make(cosf(angle) * 260.0f, sinf(angle) * 260.0f);
        at_color tint  = at_color_rgba(0.5f + 0.5f * cosf(AT_TAU * hue),
                                       0.5f + 0.5f * cosf(AT_TAU * (hue + 0.33f)),
                                       0.5f + 0.5f * cosf(AT_TAU * (hue + 0.66f)), 0.8f);

        at_renderer2d_draw_sprite(r2d, glow, where, at_vec2_make(96.0f, 96.0f), 0.0f, tint);
    }
}

/* The loaded image spinning in the middle, at most 256 units wide or tall */
static void draw_image(at_renderer2d *r2d, at_texture *image, float time) {
    uint32_t width  = 0;
    uint32_t height = 0;
    at_texture_get_size(image, &width, &height);

    float scale = 256.0f / (float)(width > height ? width : height);
    at_vec2 size = at_vec2_make((float)width * scale, (float)height * scale);

    at_renderer2d_draw_sprite(r2d, image, at_vec2_zero(), size, time * 0.3f, at_color_white());
}

/* Slimes hopping left and right. All the shadows are drawn first, then all */
/* the slimes: two batches instead of one per slime and one per shadow.     */
static void draw_walkers(at_renderer2d *r2d, at_texture *slime, float time) {
    const float ground = -200.0f;
    at_vec2     feet[WALKER_COUNT];
    bool        facing_left[WALKER_COUNT];

    for (int i = 0; i < WALKER_COUNT; ++i) {
        float phase  = (float)i * 1.3f;
        float speed  = 0.6f + 0.15f * (float)i;
        float base_x = -400.0f + (float)i * 200.0f;

        /* Moving left when the position is going down */
        float x  = base_x + sinf(time * speed + phase) * 90.0f;
        float up = fabsf(sinf(time * 6.0f + phase)) * 24.0f;

        feet[i]        = at_vec2_make(x, ground + up);
        facing_left[i] = cosf(time * speed + phase) < 0.0f;

        /* Shadow: smaller and lighter the higher the slime is */
        float shrink = 1.0f - up / 48.0f;
        at_renderer2d_draw_rect(r2d, at_vec2_make(x, ground + 2.0f),
                                at_vec2_make(48.0f * shrink, 6.0f), 0.0f,
                                at_color_rgba(0.0f, 0.0f, 0.0f, 0.35f * shrink));
    }

    for (int i = 0; i < WALKER_COUNT; ++i) {
        /* Origin at the bottom center: position is where the feet are */
        at_sprite_desc sprite = at_sprite_desc_make(slime, feet[i]);
        sprite.size   = at_vec2_make(SLIME_SIZE * SLIME_SCALE, SLIME_SIZE * SLIME_SCALE);
        sprite.origin = at_vec2_make(0.5f, 0.0f);
        sprite.flip_x = facing_left[i]; /* The texture looks right */
        at_renderer2d_draw_sprite_ex(r2d, &sprite);
    }
}

/* A row showing the sprite options: tint, alpha, rotation and flips */
static void draw_options(at_renderer2d *r2d, at_texture *slime, float time) {
    const float y    = -360.0f;
    const float size = SLIME_SIZE * SLIME_SCALE;

    at_color tints[4] = {
        at_color_hex(0xFFFFFFFF), /* Original colors */
        at_color_hex(0xFF7070FF), /* Red tint        */
        at_color_hex(0x7090FFFF), /* Blue tint       */
        at_color_hex(0xFFFFFF60), /* Half invisible  */
    };

    for (int i = 0; i < 4; ++i) {
        at_renderer2d_draw_sprite(r2d, slime, at_vec2_make(-420.0f + (float)i * 100.0f, y),
                                  at_vec2_make(size, size), 0.0f, tints[i]);
    }

    /* Turning around its center */
    at_renderer2d_draw_sprite(r2d, slime, at_vec2_make(30.0f, y), at_vec2_make(size, size),
                              time, at_color_white());

    /* Turning around its bottom-left corner */
    at_sprite_desc corner = at_sprite_desc_make(slime, at_vec2_make(130.0f, y - size * 0.5f));
    corner.size     = at_vec2_make(size, size);
    corner.origin   = at_vec2_zero();
    corner.rotation = sinf(time) * 0.5f;
    at_renderer2d_draw_sprite_ex(r2d, &corner);

    /* Upside down, and with the texture size (16x16, small) */
    at_sprite_desc upside_down = at_sprite_desc_make(slime, at_vec2_make(300.0f, y));
    upside_down.size   = at_vec2_make(size, size);
    upside_down.flip_y = true;
    at_renderer2d_draw_sprite_ex(r2d, &upside_down);

    at_sprite_desc tiny = at_sprite_desc_make(slime, at_vec2_make(400.0f, y));
    at_renderer2d_draw_sprite_ex(r2d, &tiny);
}

static void draw_scene(at_renderer2d *r2d, const scene_textures *textures, float time) {
    draw_glows(r2d, textures->glow, time);
    if (textures->image) {
        draw_image(r2d, textures->image, time);
    }
    draw_walkers(r2d, textures->slime, time);
    draw_options(r2d, textures->slime, time);
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
        .app_name          = "Sprites 2D Example",
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

    scene_textures textures;
    textures.slime = create_slime_texture(renderer);
    textures.glow  = create_glow_texture(renderer);
    textures.image = at_texture_load(renderer, "textures/image.png", NULL);
    if (!textures.image) {
        printf("No textures/image.png found: the middle stays empty\n");
    }

    if (r2d && textures.slime && textures.glow) {
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
            draw_scene(r2d, &textures, (float)now);
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
    at_texture_destroy(renderer, textures.image);
    at_texture_destroy(renderer, textures.glow);
    at_texture_destroy(renderer, textures.slime);
    at_renderer2d_destroy(r2d);
    at_renderer_destroy(renderer);
    at_window_destroy(window);
    at_platform_shutdown();
    return 0;
}