/*
 * Sprite sheets and texture atlas example.
 *   Left mouse button (drag): move the camera
 *   Mouse wheel:              zoom toward the cursor
 *   Right mouse button:       reset the camera
 *
 * Top left:  a sprite sheet made in code (4 x 2 frames of 16x16, with a
 *            margin and spacing), drawn whole with its frame grid. The
 *            frames being played are outlined in yellow.
 * Bottom:    slimes playing the two animations of the sheet.
 * Top right: a texture atlas with images of different sizes packed in one
 *            texture, drawn whole with its regions outlined.
 * Middle:    hundreds of sprites from the atlas (hearts, coins, glows,
 *            slimes): different images, one texture, so one batch.
 *
 * If examples/c/texture/image.png exists, it is added to the atlas too
 * (when it fits).
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <archtype/archtype.h>

#define WINDOW_TITLE "Archtype - Sprite sheets and atlas"

/* Sprite sheet: 4 columns x 2 rows of 16x16 frames */
#define FRAME_SIZE    16
#define SHEET_COLUMNS 4
#define SHEET_ROWS    2
#define SHEET_MARGIN  1
#define SHEET_SPACING 2
#define SHEET_WIDTH   (2 * SHEET_MARGIN + SHEET_COLUMNS * FRAME_SIZE + (SHEET_COLUMNS - 1) * SHEET_SPACING)
#define SHEET_HEIGHT  (2 * SHEET_MARGIN + SHEET_ROWS * FRAME_SIZE + (SHEET_ROWS - 1) * SHEET_SPACING)

#define ATLAS_SIZE    256
#define GLOW_SIZE     32
#define FIELD_COUNT   400

#define ZOOM_STEP 1.1f
#define MIN_ZOOM  0.1f
#define MAX_ZOOM  20.0f

/* ------------------------------------------------------------------------ */
/* Pixel art made in code                                                    */
/* ------------------------------------------------------------------------ */

static void palette(char c, uint8_t out[4]) {
    uint32_t rgba;
    switch (c) {
        case 'K': rgba = 0x1B1B2AFF; break; /* Outline        */
        case 'g': rgba = 0x5BC05BFF; break; /* Slime          */
        case 'd': rgba = 0x3A8A3AFF; break; /* Slime shadow   */
        case 'L': rgba = 0xB4F0B4FF; break; /* Slime shine    */
        case 'W': rgba = 0xFFFFFFFF; break; /* White          */
        case 'r': rgba = 0xE04848FF; break; /* Heart          */
        case 'y': rgba = 0xF2C14EFF; break; /* Coin           */
        case 'o': rgba = 0xC08020FF; break; /* Coin shadow    */
        default:  rgba = 0x00000000; break; /* Empty          */
    }
    out[0] = (uint8_t)(rgba >> 24);
    out[1] = (uint8_t)(rgba >> 16);
    out[2] = (uint8_t)(rgba >> 8);
    out[3] = (uint8_t)rgba;
}

/* Rows from top to bottom -> RGBA pixels */
static void rows_to_pixels(const char *const *rows, int width, int height, uint8_t *pixels) {
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            palette(rows[y][x], &pixels[(y * width + x) * 4]);
        }
    }
}

static const char *const k_slime_rows[FRAME_SIZE] = {
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

static const char *const k_heart_rows[8] = {
    ".KK...KK.",
    "KrrK.KrrK",
    "KrWrKrrrK",
    "KrrrrrrrK",
    ".KrrrrrK.",
    "..KrrrK..",
    "...KrK...",
    "....K....",
};

static const char *const k_coin_rows[8] = {
    "..KKKK..",
    ".KyyyyK.",
    "KyWyyyyK",
    "KyWyyyyK",
    "KyyyyyyK",
    "KyyyyyoK",
    ".KyyyoK.",
    "..KKKK..",
};

/* One slime frame: lifted `lift` texels (bouncing) and with the eyes */
/* open (blink 0), half closed (1) or closed (2).                     */
static char slime_texel(int row, int column, int lift, int blink) {
    int source = row + lift;
    if (source >= FRAME_SIZE) {
        return '.';
    }

    char c   = k_slime_rows[source][column];
    bool eye = column == 5 || column == 6 || column == 9 || column == 10;

    if (eye && blink >= 1 && source == 9) {
        return blink == 1 ? 'K' : 'g'; /* Upper eyelid */
    }
    if (eye && blink == 2 && source == 10) {
        return 'K';                    /* Closed eye: a line */
    }
    return c;
}

static void slime_frame(int lift, int blink, uint8_t *pixels, int stride, int x0, int y0) {
    for (int y = 0; y < FRAME_SIZE; ++y) {
        for (int x = 0; x < FRAME_SIZE; ++x) {
            palette(slime_texel(y, x, lift, blink), &pixels[((y0 + y) * stride + x0 + x) * 4]);
        }
    }
}

/* Row 0: bounce (lifted 0, 1, 2, 1). Row 1: blink (open, half, closed, half). */
static at_texture *create_sheet_texture(at_renderer *renderer) {
    static uint8_t pixels[SHEET_WIDTH * SHEET_HEIGHT * 4]; /* Starts transparent */
    const int      steps[SHEET_COLUMNS] = { 0, 1, 2, 1 };

    for (int column = 0; column < SHEET_COLUMNS; ++column) {
        int x = SHEET_MARGIN + column * (FRAME_SIZE + SHEET_SPACING);
        slime_frame(steps[column], 0, pixels, SHEET_WIDTH, x, SHEET_MARGIN);
        slime_frame(0, steps[column], pixels, SHEET_WIDTH, x,
                    SHEET_MARGIN + FRAME_SIZE + SHEET_SPACING);
    }

    at_texture_options options = {
        .filter = AT_TEXTURE_FILTER_NEAREST,
        .wrap   = AT_TEXTURE_WRAP_CLAMP,
    };
    return at_texture_create(renderer, SHEET_WIDTH, SHEET_HEIGHT, pixels, &options);
}

/* ------------------------------------------------------------------------ */
/* Atlas                                                                     */
/* ------------------------------------------------------------------------ */

typedef struct atlas_regions {
    int32_t glow;
    int32_t slime;
    int32_t heart;
    int32_t coin;
    int32_t image; /* -1 if there is no image */
} atlas_regions;

static at_texture_atlas *create_atlas(at_renderer *renderer, atlas_regions *regions) {
    at_texture_atlas *atlas = at_texture_atlas_create(ATLAS_SIZE, ATLAS_SIZE, 1);
    if (!atlas) {
        return NULL;
    }

    /* Tallest first: wastes less space */
    regions->image = at_texture_atlas_add_file(atlas, "textures/image.png");
    if (regions->image < 0) {
        printf("No textures/image.png in the atlas (missing or too big for %dx%d)\n",
               ATLAS_SIZE, ATLAS_SIZE);
    }

    static uint8_t glow[GLOW_SIZE * GLOW_SIZE * 4];
    for (int y = 0; y < GLOW_SIZE; ++y) {
        for (int x = 0; x < GLOW_SIZE; ++x) {
            float dx = ((float)x + 0.5f) / GLOW_SIZE * 2.0f - 1.0f;
            float dy = ((float)y + 0.5f) / GLOW_SIZE * 2.0f - 1.0f;
            float d  = at_clampf(sqrtf(dx * dx + dy * dy), 0.0f, 1.0f);

            uint8_t *p = &glow[(y * GLOW_SIZE + x) * 4];
            p[0] = p[1] = p[2] = 255;
            p[3] = (uint8_t)((1.0f - d) * (1.0f - d) * 255.0f + 0.5f);
        }
    }
    regions->glow = at_texture_atlas_add(atlas, glow, GLOW_SIZE, GLOW_SIZE);

    uint8_t slime[FRAME_SIZE * FRAME_SIZE * 4];
    slime_frame(0, 0, slime, FRAME_SIZE, 0, 0);
    regions->slime = at_texture_atlas_add(atlas, slime, FRAME_SIZE, FRAME_SIZE);

    uint8_t heart[9 * 8 * 4];
    rows_to_pixels(k_heart_rows, 9, 8, heart);
    regions->heart = at_texture_atlas_add(atlas, heart, 9, 8);

    uint8_t coin[8 * 8 * 4];
    rows_to_pixels(k_coin_rows, 8, 8, coin);
    regions->coin = at_texture_atlas_add(atlas, coin, 8, 8);

    /* Nearest for the pixel art; the padding keeps neighbours from bleeding */
    at_texture_options options = {
        .filter = AT_TEXTURE_FILTER_NEAREST,
        .wrap   = AT_TEXTURE_WRAP_CLAMP,
    };
    if (regions->glow < 0 || regions->slime < 0 || regions->heart < 0 || regions->coin < 0 ||
        !at_texture_atlas_build(atlas, renderer, &options)) {
        at_texture_atlas_destroy(renderer, atlas);
        return NULL;
    }
    return atlas;
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
/* Drawing helpers                                                           */
/* ------------------------------------------------------------------------ */

/* Outline of a texel rectangle of an image drawn with its top-left corner */
/* at top_left and `scale` world units per texel (texel Y goes down).      */
static void outline_texels(at_renderer2d *r2d, at_vec2 top_left, float scale, at_rect texels,
                           float thickness, at_color color) {
    float x0 = top_left.x + texels.x * scale;
    float x1 = top_left.x + (texels.x + texels.width) * scale;
    float y0 = top_left.y - texels.y * scale;
    float y1 = top_left.y - (texels.y + texels.height) * scale;

    at_renderer2d_draw_line(r2d, at_vec2_make(x0, y0), at_vec2_make(x1, y0), thickness, color);
    at_renderer2d_draw_line(r2d, at_vec2_make(x0, y1), at_vec2_make(x1, y1), thickness, color);
    at_renderer2d_draw_line(r2d, at_vec2_make(x0, y0), at_vec2_make(x0, y1), thickness, color);
    at_renderer2d_draw_line(r2d, at_vec2_make(x1, y0), at_vec2_make(x1, y1), thickness, color);
}

/* A whole texture drawn with its top-left corner at top_left, on a dark */
/* background so its transparent parts show.                             */
static void draw_whole_texture(at_renderer2d *r2d, at_texture *texture, at_vec2 top_left,
                               float scale) {
    uint32_t width  = 0;
    uint32_t height = 0;
    at_texture_get_size(texture, &width, &height);

    at_vec2 size = at_vec2_make((float)width * scale, (float)height * scale);

    at_renderer2d_draw_rect(r2d, at_vec2_make(top_left.x + size.x * 0.5f,
                                              top_left.y - size.y * 0.5f),
                            size, 0.0f, at_color_hex(0x000000A0));

    at_sprite_desc sprite = at_sprite_desc_make(texture, top_left);
    sprite.size   = size;
    sprite.origin = at_vec2_make(0.0f, 1.0f); /* Top-left corner */
    at_renderer2d_draw_sprite_ex(r2d, &sprite);
}

/* ------------------------------------------------------------------------ */
/* Scene                                                                     */
/* ------------------------------------------------------------------------ */

typedef struct scene {
    at_sprite_sheet   sheet;
    at_animation      bounce;
    at_animation      blink;
    at_texture_atlas *atlas;
    atlas_regions     regions;
} scene;

/* Blink every 3 seconds: plays the blink animation once, then stays open */
static uint32_t blink_frame(const scene *s, float time) {
    float since = fmodf(time, 3.0f);
    return at_animation_is_finished(&s->blink, since) ? s->blink.first_frame
                                                      : at_animation_frame(&s->blink, since);
}

static void draw_sheet(at_renderer2d *r2d, const scene *s, float time, float pixel) {
    const float   scale    = 4.0f;
    const at_vec2 top_left = at_vec2_make(-600.0f, 330.0f);

    at_renderer2d_set_layer(r2d, 0);
    draw_whole_texture(r2d, s->sheet.texture, top_left, scale);

    /* Frame grid, and the frames being played now in yellow */
    at_renderer2d_set_layer(r2d, 1);
    for (uint32_t i = 0; i < at_sprite_sheet_frame_count(&s->sheet); ++i) {
        outline_texels(r2d, top_left, scale, at_sprite_sheet_frame(&s->sheet, i), pixel,
                       at_color_hex(0x8080A0FF));
    }
    outline_texels(r2d, top_left, scale,
                   at_sprite_sheet_frame(&s->sheet, at_animation_frame(&s->bounce, time)),
                   2.0f * pixel, at_color_hex(0xF2C14EFF));
    outline_texels(r2d, top_left, scale,
                   at_sprite_sheet_frame(&s->sheet, blink_frame(s, time)),
                   2.0f * pixel, at_color_hex(0xF2C14EFF));
}

static void draw_animated_slimes(at_renderer2d *r2d, const scene *s, float time) {
    at_renderer2d_set_layer(r2d, 0);

    /* Bouncing at different speeds */
    for (int i = 0; i < 6; ++i) {
        at_animation bounce = s->bounce;
        bounce.fps = 4.0f + 2.0f * (float)i;

        at_sprite_desc sprite = at_sprite_sheet_sprite(&s->sheet, at_animation_frame(&bounce, time),
                                                       at_vec2_make(-600.0f + (float)i * 90.0f,
                                                                    -340.0f));
        sprite.size   = at_vec2_make(64.0f, 64.0f);
        sprite.origin = at_vec2_make(0.5f, 0.0f);
        at_renderer2d_draw_sprite_ex(r2d, &sprite);
    }

    /* Blinking, big */
    at_sprite_desc blinker = at_sprite_sheet_sprite(&s->sheet, blink_frame(s, time),
                                                    at_vec2_make(-560.0f, -60.0f));
    blinker.size   = at_vec2_make(128.0f, 128.0f);
    blinker.origin = at_vec2_make(0.5f, 0.0f);
    at_renderer2d_draw_sprite_ex(r2d, &blinker);
}

static void draw_atlas(at_renderer2d *r2d, const scene *s, float pixel) {
    const float   scale    = 1.5f;
    const at_vec2 top_left = at_vec2_make(250.0f, 330.0f);
    at_texture   *texture  = at_texture_atlas_get_texture(s->atlas);

    at_renderer2d_set_layer(r2d, 0);
    draw_whole_texture(r2d, texture, top_left, scale);

    /* The whole atlas area, and each region */
    at_renderer2d_set_layer(r2d, 1);
    outline_texels(r2d, top_left, scale, at_rect_make(0.0f, 0.0f, ATLAS_SIZE, ATLAS_SIZE),
                   pixel, at_color_hex(0x8080A0FF));
    for (uint32_t i = 0; i < at_texture_atlas_get_region_count(s->atlas); ++i) {
        outline_texels(r2d, top_left, scale, at_texture_atlas_get_region(s->atlas, i), pixel,
                       at_color_hex(0xF2C14EFF));
    }
}

/* Hundreds of sprites from four different images of the atlas, in one */
/* layer: they all use the atlas texture, so they are one batch.        */
static void draw_field(at_renderer2d *r2d, const scene *s, float time) {
    const uint32_t kinds[4] = {
        (uint32_t)s->regions.heart, (uint32_t)s->regions.coin,
        (uint32_t)s->regions.slime, (uint32_t)s->regions.glow,
    };
    const float sizes[4] = { 27.0f, 24.0f, 32.0f, 40.0f };

    at_renderer2d_set_layer(r2d, 2);
    for (int i = 0; i < FIELD_COUNT; ++i) {
        int kind = i % 4;

        /* A flattened spiral (golden angle) that slowly turns */
        float   angle  = (float)i * 2.399963f + time * (0.1f + 0.002f * (float)(i % 50));
        float   radius = 30.0f + 18.0f * sqrtf((float)i);
        at_vec2 where  = at_vec2_make(-100.0f + cosf(angle) * radius * 0.7f,
                                      -120.0f + sinf(angle) * radius * 0.3f);

        at_sprite_desc sprite = at_texture_atlas_sprite(s->atlas, kinds[kind], where);
        sprite.size = at_vec2_make(sizes[kind], sizes[kind]);
        if (kind == 3) {
            /* Glows get a color from the tint */
            float hue = (float)i / FIELD_COUNT;
            sprite.tint = at_color_rgba(0.5f + 0.5f * cosf(AT_TAU * hue),
                                        0.5f + 0.5f * cosf(AT_TAU * (hue + 0.33f)),
                                        0.5f + 0.5f * cosf(AT_TAU * (hue + 0.66f)), 0.7f);
        } else if (kind == 1) {
            /* Coins spin: squeeze them sideways */
            sprite.size.x *= fabsf(cosf(time * 3.0f + (float)i));
        }
        at_renderer2d_draw_sprite_ex(r2d, &sprite);
    }
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
        .app_name          = "Atlas 2D Example",
        .enable_validation = true,
        .vsync             = true,
    };

    at_renderer *renderer = at_renderer_create(&renderer_desc);
    if (!renderer) {
        at_window_destroy(window);
        at_platform_shutdown();
        return 1;
    }

    at_renderer2d *r2d           = at_renderer2d_create(renderer, NULL);
    at_texture    *sheet_texture = create_sheet_texture(renderer);

    scene s;
    memset(&s, 0, sizeof(s));
    s.sheet  = at_sprite_sheet_make(sheet_texture, FRAME_SIZE, FRAME_SIZE,
                                    SHEET_MARGIN, SHEET_SPACING);
    s.bounce = at_animation_make(0, 4, 8.0f, true);   /* Row 0             */
    s.blink  = at_animation_make(4, 4, 12.0f, false); /* Row 1, plays once */
    s.atlas  = create_atlas(renderer, &s.regions);

    if (r2d && sheet_texture && s.atlas) {
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

            float time  = (float)now;
            float pixel = 1.0f / camera.zoom;

            at_renderer2d_begin(r2d, &camera);
            draw_sheet(r2d, &s, time, pixel);
            draw_animated_slimes(r2d, &s, time);
            draw_atlas(r2d, &s, pixel);
            draw_field(r2d, &s, time);
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
    at_texture_atlas_destroy(renderer, s.atlas);
    at_texture_destroy(renderer, sheet_texture);
    at_renderer2d_destroy(r2d);
    at_renderer_destroy(renderer);
    at_window_destroy(window);
    at_platform_shutdown();
    return 0;
}