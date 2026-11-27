/*
 * 2D batching stress test: thousands of bouncing sprites.
 *   Left mouse button (hold): add sprites at the cursor
 *   Right mouse button:       remove all sprites
 *   Middle mouse button:      switch between one texture atlas and one
 *                             texture per image
 *
 * The title shows the sprite count, the draw calls of the frame and the FPS
 * (vsync is off). With the atlas, every sprite uses the same texture, so the
 * whole scene is a single draw call. With separate textures, the texture
 * changes from one sprite to the next and almost every sprite becomes a
 * draw call of its own: that is what batching saves.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <archtype/archtype.h>

#define WINDOW_TITLE "Archtype - Stress 2D"

#define MAX_SPRITES     100000
#define START_SPRITES   2000
#define SPAWN_PER_FRAME 250
#define KIND_COUNT      4
#define GRAVITY         -900.0f /* World units per second squared */

/* ------------------------------------------------------------------------ */
/* Images made in code                                                       */
/* ------------------------------------------------------------------------ */

static void palette(char c, uint8_t out[4]) {
    uint32_t rgba;
    switch (c) {
        case 'K': rgba = 0x1B1B2AFF; break;
        case 'g': rgba = 0x5BC05BFF; break;
        case 'd': rgba = 0x3A8A3AFF; break;
        case 'L': rgba = 0xB4F0B4FF; break;
        case 'W': rgba = 0xFFFFFFFF; break;
        case 'r': rgba = 0xE04848FF; break;
        case 'y': rgba = 0xF2C14EFF; break;
        case 'o': rgba = 0xC08020FF; break;
        case 'b': rgba = 0x4D9DE0FF; break;
        default:  rgba = 0x00000000; break;
    }
    out[0] = (uint8_t)(rgba >> 24);
    out[1] = (uint8_t)(rgba >> 16);
    out[2] = (uint8_t)(rgba >> 8);
    out[3] = (uint8_t)rgba;
}

static const char *const k_slime[16] = {
    "................", "................", "................", "................",
    "................", "......KKKK......", "....KKggggKK....", "...KgggggLLgK...",
    "..KggggggggLgK..", "..KggWKggWKggK..", ".KgggWKggWKgggK.", ".KggggggggggggK.",
    ".KgggggKKgggggK.", ".KddggggggggddK.", ".KddddddddddddK.", "..KKKKKKKKKKKK..",
};

static const char *const k_heart[8] = {
    ".KK...KK.", "KrrK.KrrK", "KrWrKrrrK", "KrrrrrrrK",
    ".KrrrrrK.", "..KrrrK..", "...KrK...", "....K....",
};

static const char *const k_coin[8] = {
    "..KKKK..", ".KyyyyK.", "KyWyyyyK", "KyWyyyyK",
    "KyyyyyyK", "KyyyyyoK", ".KyyyoK.", "..KKKK..",
};

static const char *const k_gem[8] = {
    "..KKKK..", ".KbWbbK.", "KbWbbbbK", "KbbbbbbK",
    ".KbbbbK.", "..KbbK..", "...KK...", "........",
};

typedef struct image {
    const char *const *rows;
    uint32_t           width;
    uint32_t           height;
} image;

static const image k_images[KIND_COUNT] = {
    { k_slime, 16, 16 },
    { k_heart, 9, 8 },
    { k_coin, 8, 8 },
    { k_gem, 8, 8 },
};

static void image_pixels(const image *img, uint8_t *pixels) {
    for (uint32_t y = 0; y < img->height; ++y) {
        for (uint32_t x = 0; x < img->width; ++x) {
            palette(img->rows[y][x], &pixels[(y * img->width + x) * 4]);
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Sprites                                                                   */
/* ------------------------------------------------------------------------ */

typedef struct sprite {
    at_vec2  position;
    at_vec2  velocity;
    float    rotation;
    float    spin;
    uint32_t kind;
} sprite;

static sprite   s_sprites[MAX_SPRITES]; /* Static: too big for the stack */
static uint32_t s_count = 0;
static uint32_t s_random = 0x12345678u;

/* xorshift: fast and good enough for an example. Returns 0..1 */
static float random01(void) {
    s_random ^= s_random << 13;
    s_random ^= s_random >> 17;
    s_random ^= s_random << 5;
    return (float)(s_random & 0xFFFFFFu) / (float)0xFFFFFFu;
}

static void spawn(at_vec2 where, uint32_t count) {
    for (uint32_t i = 0; i < count && s_count < MAX_SPRITES; ++i) {
        sprite *s   = &s_sprites[s_count++];
        s->position = where;
        s->velocity = at_vec2_make((random01() - 0.5f) * 600.0f, 200.0f + random01() * 500.0f);
        s->rotation = 0.0f;
        s->spin     = (random01() - 0.5f) * 8.0f;
        s->kind     = s_count % KIND_COUNT; /* Neighbours always differ */
    }
}

/* Bounces inside the visible area (default camera: origin at the center) */
static void update_sprites(float dt, float half_width, float half_height) {
    for (uint32_t i = 0; i < s_count; ++i) {
        sprite *s = &s_sprites[i];

        s->velocity.y += GRAVITY * dt;
        s->position    = at_vec2_add(s->position, at_vec2_scale(s->velocity, dt));
        s->rotation   += s->spin * dt;

        if (s->position.x < -half_width) {
            s->position.x = -half_width;
            s->velocity.x = fabsf(s->velocity.x);
        } else if (s->position.x > half_width) {
            s->position.x = half_width;
            s->velocity.x = -fabsf(s->velocity.x);
        }

        if (s->position.y < -half_height) {
            /* Lose some speed on each bounce, with a random kick now and then */
            s->position.y = -half_height;
            s->velocity.y = fabsf(s->velocity.y) * 0.85f;
            if (random01() < 0.1f) {
                s->velocity.y += 300.0f + random01() * 400.0f;
            }
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Main                                                                      */
/* ------------------------------------------------------------------------ */

typedef struct textures {
    at_texture_atlas *atlas;
    uint32_t          regions[KIND_COUNT];
    at_texture       *separate[KIND_COUNT];
} textures;

/* On failure, what was created stays in t for destroy_textures */
static bool create_textures(at_renderer *renderer, textures *t) {
    at_texture_options options = {
        .filter = AT_TEXTURE_FILTER_NEAREST,
        .wrap   = AT_TEXTURE_WRAP_CLAMP,
    };

    t->atlas = at_texture_atlas_create(64, 64, 1);
    if (!t->atlas) {
        return false;
    }

    for (uint32_t kind = 0; kind < KIND_COUNT; ++kind) {
        uint8_t pixels[16 * 16 * 4];
        image_pixels(&k_images[kind], pixels);

        /* The same image twice: in the atlas and as a texture of its own */
        int32_t region = at_texture_atlas_add(t->atlas, pixels, k_images[kind].width,
                                              k_images[kind].height);
        t->separate[kind] = at_texture_create(renderer, k_images[kind].width,
                                              k_images[kind].height, pixels, &options);
        if (region < 0 || !t->separate[kind]) {
            return false;
        }
        t->regions[kind] = (uint32_t)region;
    }

    return at_texture_atlas_build(t->atlas, renderer, &options);
}

static void destroy_textures(at_renderer *renderer, textures *t) {
    for (uint32_t kind = 0; kind < KIND_COUNT; ++kind) {
        at_texture_destroy(renderer, t->separate[kind]);
    }
    at_texture_atlas_destroy(renderer, t->atlas);
}

static void draw_sprites(at_renderer2d *r2d, const textures *t, bool use_atlas) {
    for (uint32_t i = 0; i < s_count; ++i) {
        const sprite *s    = &s_sprites[i];
        const image  *img  = &k_images[s->kind];

        at_sprite_desc desc = use_atlas
                            ? at_texture_atlas_sprite(t->atlas, t->regions[s->kind], s->position)
                            : at_sprite_desc_make(t->separate[s->kind], s->position);
        desc.size     = at_vec2_make((float)img->width * 3.0f, (float)img->height * 3.0f);
        desc.rotation = s->rotation;
        at_renderer2d_draw_sprite_ex(r2d, &desc);
    }
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

    /* vsync off, so the FPS shows how fast it really is */
    at_renderer_desc renderer_desc = {
        .window            = window,
        .app_name          = "Stress 2D Example",
        .enable_validation = false, /* Validation slows thousands of draw calls a lot */
        .vsync             = false,
    };

    at_renderer *renderer = at_renderer_create(&renderer_desc);
    if (!renderer) {
        at_window_destroy(window);
        at_platform_shutdown();
        return 1;
    }

    /* Room for every sprite (4 vertices each), plus a little spare */
    at_renderer2d_desc r2d_desc = { .max_vertices = MAX_SPRITES * 4 + 1024 };
    at_renderer2d     *r2d      = at_renderer2d_create(renderer, &r2d_desc);

    textures t;
    memset(&t, 0, sizeof(t));
    bool ok = r2d && create_textures(renderer, &t);

    if (ok) {
        at_renderer_set_clear_color(renderer, 0.08f, 0.08f, 0.10f, 1.0f);

        at_camera2d camera     = at_camera2d_make();
        bool        use_atlas  = true;
        bool        was_right  = false;
        bool        was_middle = false;
        double      last_time  = at_platform_get_time();
        double      fps_timer  = 0.0;
        uint32_t    frames     = 0;

        spawn(at_vec2_zero(), START_SPRITES);

        while (!at_window_should_close(window)) {
            at_platform_poll_events();

            if (at_window_is_minimized(window)) {
                at_platform_wait_events();
                last_time = at_platform_get_time();
                continue;
            }

            double now = at_platform_get_time();
            float  dt  = at_minf((float)(now - last_time), 0.05f); /* Avoid huge steps */
            fps_timer += now - last_time;
            last_time  = now;

            if (!at_renderer_begin_frame(renderer)) {
                continue;
            }

            uint32_t width  = 0;
            uint32_t height = 0;
            at_renderer_get_size(renderer, &width, &height);

            /* Input: hold left to add, click right to clear, middle to switch */
            at_vec2 mouse;
            at_window_get_mouse_position(window, &mouse.x, &mouse.y);

            if (at_window_is_mouse_button_down(window, AT_MOUSE_BUTTON_LEFT)) {
                spawn(at_camera2d_screen_to_world(&camera, mouse, (float)width, (float)height),
                      SPAWN_PER_FRAME);
            }

            bool right = at_window_is_mouse_button_down(window, AT_MOUSE_BUTTON_RIGHT);
            if (right && !was_right) {
                s_count = 0;
            }
            was_right = right;

            bool middle = at_window_is_mouse_button_down(window, AT_MOUSE_BUTTON_MIDDLE);
            if (middle && !was_middle) {
                use_atlas = !use_atlas;
            }
            was_middle = middle;

            update_sprites(dt, (float)width * 0.5f, (float)height * 0.5f);

            at_renderer2d_begin(r2d, &camera);
            draw_sprites(r2d, &t, use_atlas);
            at_renderer2d_end(r2d);

            /* Read the stats after the last end of the frame */
            at_renderer2d_stats stats = at_renderer2d_get_stats(r2d);

            at_renderer_end_frame(renderer);

            frames++;
            if (fps_timer >= 0.5) {
                char title[200];
                snprintf(title, sizeof(title),
                         WINDOW_TITLE " | %u sprites | %u draw calls | %u FPS | %s "
                         "(middle click to switch)",
                         (unsigned)s_count, (unsigned)stats.draw_calls,
                         (unsigned)((double)frames / fps_timer),
                         use_atlas ? "atlas" : "separate textures");
                at_window_set_title(window, title);
                fps_timer = 0.0;
                frames    = 0;
            }
        }
    }

    /* Destroy in reverse order of creation (NULL textures are skipped) */
    destroy_textures(renderer, &t);
    at_renderer2d_destroy(r2d);
    at_renderer_destroy(renderer);
    at_window_destroy(window);
    at_platform_shutdown();
    return 0;
}