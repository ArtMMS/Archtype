/*
 * 3D materials example: what at_material can do, with the engine's 3D
 * renderer.
 *   Right mouse button (hold): look around
 *   W A S D:                   move
 *   E or Space / Q or Ctrl:    up / down
 *   Shift (hold):              faster
 *   Mouse wheel:               change the speed
 *   M:                         floor texture with or without mipmaps
 *   R:                         reset the camera
 *   Esc:                       quit
 *
 * Back row:   from matte to very shiny (specular and shininess)
 * Middle row: textures, a tinted texture, emission and unlit
 * Front:      a double-sided panel, a fence with holes (alpha cutoff) and
 *             three glass spheres (transparent, sorted back to front)
 * Floor:      a checkerboard repeated with uv_scale. Without mipmaps it
 *             shimmers in the distance; with them it fades out smoothly.
 *
 * The textures are made in code. Labels are 2D text placed over each
 * object with at_camera3d_world_to_screen.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <archtype/archtype.h>

#define WINDOW_TITLE "Archtype - Materials 3D"
#define TEXTURE_SIZE 64

/* ------------------------------------------------------------------------ */
/* Textures made in code                                                     */
/* ------------------------------------------------------------------------ */

static void set_pixel(uint8_t *pixels, int x, int y, uint32_t rgba) {
    uint8_t *p = &pixels[(y * TEXTURE_SIZE + x) * 4];
    p[0] = (uint8_t)(rgba >> 24);
    p[1] = (uint8_t)(rgba >> 16);
    p[2] = (uint8_t)(rgba >> 8);
    p[3] = (uint8_t)rgba;
}

/* 8 x 8 squares, light and dark */
static void fill_checker(uint8_t *pixels) {
    for (int y = 0; y < TEXTURE_SIZE; ++y) {
        for (int x = 0; x < TEXTURE_SIZE; ++x) {
            bool light = ((x / 8) + (y / 8)) % 2 == 0;
            set_pixel(pixels, x, y, light ? 0xD8D8D8FFu : 0x50505AFFu);
        }
    }
}

/* Rows of bricks, every other row shifted by half a brick, with mortar */
static void fill_bricks(uint8_t *pixels) {
    const uint32_t shades[3] = { 0xA0442EFFu, 0x8E3A28FFu, 0xB04E34FFu };

    for (int y = 0; y < TEXTURE_SIZE; ++y) {
        int row     = y / 16;
        int local_y = y % 16;

        for (int x = 0; x < TEXTURE_SIZE; ++x) {
            int shifted = x + (row % 2) * 16;
            int local_x = shifted % 32;
            int brick   = (shifted / 32 + row * 3) % 3;

            bool mortar = local_y < 2 || local_x < 2;
            set_pixel(pixels, x, y, mortar ? 0xC8C0B0FFu : shades[brick]);
        }
    }
}

/* Wooden bars with square holes between them. The holes keep the wood */
/* color with alpha 0, so filtering doesn't darken the edges.          */
static void fill_fence(uint8_t *pixels) {
    for (int y = 0; y < TEXTURE_SIZE; ++y) {
        for (int x = 0; x < TEXTURE_SIZE; ++x) {
            bool bar = x % 16 < 6 || y % 16 < 6;
            set_pixel(pixels, x, y, bar ? 0x8A6A44FFu : 0x8A6A4400u);
        }
    }
}

static at_texture *make_texture(at_renderer *renderer, void (*fill)(uint8_t *), bool mipmaps) {
    static uint8_t pixels[TEXTURE_SIZE * TEXTURE_SIZE * 4];
    fill(pixels);

    at_texture_options options = { .mipmaps = mipmaps }; /* Linear, repeat, sRGB */
    return at_texture_create(renderer, TEXTURE_SIZE, TEXTURE_SIZE, pixels, &options);
}

/* ------------------------------------------------------------------------ */
/* Scene                                                                     */
/* ------------------------------------------------------------------------ */

typedef struct scene_assets {
    at_mesh    *floor;
    at_mesh    *cube;
    at_mesh    *sphere;
    at_mesh    *panel;          /* A 2 x 2 plane, stood up */
    at_texture *checker;        /* With mipmaps            */
    at_texture *checker_no_mip; /* The same, without them  */
    at_texture *bricks;
    at_texture *fence;
} scene_assets;

static bool create_assets(at_renderer *renderer, scene_assets *assets) {
    assets->floor          = at_mesh_create_plane(renderer, 60.0f, 60.0f, 1, 1);
    assets->cube           = at_mesh_create_cube(renderer, at_vec3_make(1.6f, 1.6f, 1.6f));
    assets->sphere         = at_mesh_create_sphere(renderer, 1.0f, 48, 24);
    assets->panel          = at_mesh_create_plane(renderer, 2.0f, 2.0f, 1, 1);
    assets->checker        = make_texture(renderer, fill_checker, true);
    assets->checker_no_mip = make_texture(renderer, fill_checker, false);
    assets->bricks         = make_texture(renderer, fill_bricks, true);
    assets->fence          = make_texture(renderer, fill_fence, true);

    return assets->floor && assets->cube && assets->sphere && assets->panel &&
           assets->checker && assets->checker_no_mip && assets->bricks && assets->fence;
}

static void destroy_assets(at_renderer *renderer, scene_assets *assets) {
    at_texture_destroy(renderer, assets->fence);
    at_texture_destroy(renderer, assets->bricks);
    at_texture_destroy(renderer, assets->checker_no_mip);
    at_texture_destroy(renderer, assets->checker);
    at_mesh_destroy(renderer, assets->panel);
    at_mesh_destroy(renderer, assets->sphere);
    at_mesh_destroy(renderer, assets->cube);
    at_mesh_destroy(renderer, assets->floor);
}

static at_mat4 make_model(at_vec3 position, at_quat rotation, float scale) {
    at_transform transform = at_transform_identity();
    transform.position     = position;
    transform.rotation     = rotation;
    transform.scale        = at_vec3_make(scale, scale, scale);
    return at_transform_to_mat4(transform);
}

/* Columns of the rows, and the labels drawn over the objects */
static const float k_columns[5] = { -6.0f, -3.0f, 0.0f, 3.0f, 6.0f };

#define LABEL_COUNT 13

typedef struct label {
    at_vec3     position;
    const char *text;
} label;

static const label k_labels[LABEL_COUNT] = {
    { { -6.0f, 2.4f, -5.0f }, "matte"         },
    { { -3.0f, 2.4f, -5.0f }, "shininess 4"   },
    { {  0.0f, 2.4f, -5.0f }, "shininess 16"  },
    { {  3.0f, 2.4f, -5.0f }, "shininess 64"  },
    { {  6.0f, 2.4f, -5.0f }, "shininess 256" },
    { { -6.0f, 2.4f, -1.0f }, "texture"       },
    { { -3.0f, 2.4f, -1.0f }, "texture"       },
    { {  0.0f, 2.4f, -1.0f }, "tinted"        },
    { {  3.0f, 2.4f, -1.0f }, "emissive"      },
    { {  6.0f, 2.4f, -1.0f }, "unlit"         },
    { { -4.5f, 3.0f,  3.5f }, "double-sided"  },
    { {  0.0f, 3.0f,  3.5f }, "transparent"   },
    { {  4.5f, 3.0f,  3.5f }, "alpha cutoff"  },
};

static void draw_floor(at_renderer3d *r3d, const scene_assets *assets, bool mipmaps) {
    /* The checkerboard repeated 20 times each way: 160 squares across */
    at_material floor  = at_material_make();
    floor.base_texture = mipmaps ? assets->checker : assets->checker_no_mip;
    floor.uv_scale     = at_vec2_make(20.0f, 20.0f);
    floor.specular     = 0.1f;

    at_renderer3d_draw_mesh_ex(r3d, assets->floor, at_mat4_identity(), &floor);
}

/* The same red sphere from matte to very shiny */
static void draw_shiny_row(at_renderer3d *r3d, const scene_assets *assets) {
    const float shininess[5] = { 1.0f, 4.0f, 16.0f, 64.0f, 256.0f };

    for (int i = 0; i < 5; ++i) {
        at_material material = at_material_make();
        material.base_color  = at_color_hex(0xC8463CFF);
        material.specular    = i == 0 ? 0.0f : 1.0f;
        material.shininess   = shininess[i];

        at_renderer3d_draw_mesh_ex(r3d, assets->sphere,
                                   make_model(at_vec3_make(k_columns[i], 1.0f, -5.0f),
                                              at_quat_identity(), 1.0f),
                                   &material);
    }
}

static void draw_surface_row(at_renderer3d *r3d, const scene_assets *assets, float time) {
    const float   z    = -1.0f;
    const at_quat spin = at_quat_from_axis_angle(at_vec3_up(), time * 0.4f);

    /* A sphere with the checkerboard */
    at_material checker  = at_material_make();
    checker.base_texture = assets->checker;
    at_renderer3d_draw_mesh_ex(r3d, assets->sphere,
                               make_model(at_vec3_make(k_columns[0], 1.0f, z), spin, 1.0f),
                               &checker);

    /* Brick cubes: as they are, and tinted blue (the texture times the color) */
    at_material bricks  = at_material_make();
    bricks.base_texture = assets->bricks;
    bricks.specular     = 0.1f;
    at_renderer3d_draw_mesh_ex(r3d, assets->cube,
                               make_model(at_vec3_make(k_columns[1], 1.0f, z), spin, 1.0f),
                               &bricks);

    bricks.base_color = at_color_hex(0x80A8FFFF);
    at_renderer3d_draw_mesh_ex(r3d, assets->cube,
                               make_model(at_vec3_make(k_columns[2], 1.0f, z), spin, 1.0f),
                               &bricks);

    /* Glowing: a dark surface that gives off a pulsing orange light */
    at_material glow       = at_material_make();
    glow.base_color        = at_color_hex(0x302018FF);
    glow.emissive          = at_color_hex(0xFF8020FF);
    glow.emissive_strength = 1.0f + 0.6f * sinf(time * 3.0f);
    at_renderer3d_draw_mesh_ex(r3d, assets->sphere,
                               make_model(at_vec3_make(k_columns[3], 1.0f, z),
                                          at_quat_identity(), 1.0f),
                               &glow);

    /* Unlit: the same flat color on every side */
    at_material flat = at_material_make();
    flat.base_color  = at_color_hex(0x7DCE82FF);
    flat.unlit       = true;
    at_renderer3d_draw_mesh_ex(r3d, assets->sphere,
                               make_model(at_vec3_make(k_columns[4], 1.0f, z),
                                          at_quat_identity(), 1.0f),
                               &flat);
}

static void draw_front(at_renderer3d *r3d, const scene_assets *assets, float time) {
    /* The panel mesh lies on the floor facing up: stand it up to face +Z, */
    /* then turn it, so both of its sides show                             */
    const at_quat stand_up = at_quat_from_axis_angle(at_vec3_right(), AT_PI * 0.5f);

    at_material panel  = at_material_make();
    panel.base_texture = assets->bricks;
    panel.double_sided = true;
    panel.specular     = 0.1f;
    at_quat turn       = at_quat_mul(at_quat_from_axis_angle(at_vec3_up(), time * 0.7f),
                                     stand_up);
    at_renderer3d_draw_mesh_ex(r3d, assets->panel,
                               make_model(at_vec3_make(-4.5f, 1.5f, 3.5f), turn, 1.0f),
                               &panel);

    /* A fence: the holes are not drawn at all (alpha below the cutoff) */
    at_material fence  = at_material_make();
    fence.base_texture = assets->fence;
    fence.uv_scale     = at_vec2_make(2.0f, 2.0f);
    fence.alpha_cutoff = 0.5f;
    fence.double_sided = true;
    fence.specular     = 0.0f;
    turn = at_quat_mul(at_quat_from_axis_angle(at_vec3_up(), time * -0.3f), stand_up);
    at_renderer3d_draw_mesh_ex(r3d, assets->panel,
                               make_model(at_vec3_make(4.5f, 1.5f, 3.5f), turn, 1.0f),
                               &fence);

    /* Glass: three see-through spheres circling each other. They are */
    /* sorted back to front every frame, so they blend correctly.     */
    const at_color tints[3] = {
        at_color_rgba(1.0f, 0.4f, 0.4f, 0.35f),
        at_color_rgba(0.4f, 1.0f, 0.5f, 0.35f),
        at_color_rgba(0.4f, 0.6f, 1.0f, 0.35f),
    };
    for (int i = 0; i < 3; ++i) {
        float   angle    = time * 0.8f + AT_TAU * (float)i / 3.0f;
        at_vec3 position = at_vec3_make(cosf(angle) * 1.1f, 1.4f, 3.5f + sinf(angle) * 1.1f);

        at_material glass = at_material_make();
        glass.base_color  = tints[i];
        glass.transparent = true;
        glass.specular    = 1.0f;
        glass.shininess   = 128.0f;
        at_renderer3d_draw_mesh_ex(r3d, assets->sphere,
                                   make_model(position, at_quat_identity(), 0.8f), &glass);
    }
}

static void draw_scene(at_renderer3d *r3d, const scene_assets *assets, float time,
                       bool floor_mipmaps) {
    draw_floor(r3d, assets, floor_mipmaps);
    draw_shiny_row(r3d, assets);
    draw_surface_row(r3d, assets, time);
    draw_front(r3d, assets, time);
}

/* ------------------------------------------------------------------------ */
/* Camera                                                                    */
/* ------------------------------------------------------------------------ */

static void reset_camera(at_camera3d *camera, at_fly_camera *fly) {
    *camera          = at_camera3d_make();
    camera->position = at_vec3_make(0.0f, 5.0f, 14.0f);
    at_camera3d_look_at(camera, at_vec3_make(0.0f, 1.0f, 0.0f), at_vec3_up());

    float speed = fly->speed; /* Keep the chosen speed */
    *fly        = at_fly_camera_make();
    fly->speed  = speed;
    at_fly_camera_sync(fly, camera);
}

/* ------------------------------------------------------------------------ */
/* HUD                                                                       */
/* ------------------------------------------------------------------------ */

/* 2D camera coordinates (origin in the middle, +Y up) of a screen pixel */
static at_vec2 screen_to_hud(at_vec2 screen, float width, float height) {
    return at_vec2_make(screen.x - width * 0.5f, height * 0.5f - screen.y);
}

/* Each label centered over its object, with a dark shadow */
static void draw_labels(at_renderer2d *r2d, const at_font *font, const at_camera3d *camera,
                        float width, float height) {
    for (int i = 0; i < LABEL_COUNT; ++i) {
        at_vec2 screen;
        if (!at_camera3d_world_to_screen(camera, k_labels[i].position, width, height,
                                         &screen)) {
            continue; /* Behind the camera */
        }

        at_text_desc text = at_text_desc_make(font, k_labels[i].text,
                                              screen_to_hud(screen, width, height));
        text.scale = 2.0f;
        text.align = AT_TEXT_ALIGN_CENTER;

        at_renderer2d_set_layer(r2d, 0);
        text.color      = at_color_rgba(0.0f, 0.0f, 0.0f, 0.7f);
        text.position.x += 2.0f;
        text.position.y -= 2.0f;
        at_renderer2d_draw_text_ex(r2d, &text);

        at_renderer2d_set_layer(r2d, 1);
        text.color      = at_color_white();
        text.position.x -= 2.0f;
        text.position.y += 2.0f;
        at_renderer2d_draw_text_ex(r2d, &text);
    }
}

static void draw_hud(at_renderer2d *r2d, const at_font *font, float width, float height,
                     const at_renderer3d_stats *stats, const at_fly_camera *fly,
                     bool floor_mipmaps, uint32_t fps) {
    const float scale   = 2.0f;
    const float margin  = 8.0f;
    const float padding = 8.0f;

    char text[512];
    snprintf(text, sizeof(text),
             "%u FPS   %u draw calls (%u transparent)   %u triangles\n"
             "Floor mipmaps: %s (M)\n"
             "\n"
             "Hold right mouse: look   WASD: move   E/Q: up/down\n"
             "Shift: faster   Wheel: speed   R: reset   Esc: quit",
             (unsigned)fps, (unsigned)stats->draw_calls, (unsigned)stats->transparent,
             (unsigned)stats->triangles, floor_mipmaps ? "ON" : "OFF");

    at_vec2 size = at_font_measure(font, text, scale);
    at_vec2 box  = at_vec2_make(size.x + 2.0f * padding, size.y + 2.0f * padding);
    float   left = -width * 0.5f + margin;
    float   top  =  height * 0.5f - margin;

    at_renderer2d_set_layer(r2d, 2);
    at_renderer2d_draw_rect(r2d, at_vec2_make(left + box.x * 0.5f, top - box.y * 0.5f), box,
                            0.0f, at_color_rgba(0.0f, 0.0f, 0.0f, 0.55f));
    at_renderer2d_set_layer(r2d, 3);
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
        .app_name          = "Materials 3D Example",
        .enable_validation = true,
        .vsync             = true,
    };

    at_renderer *renderer = at_renderer_create(&renderer_desc);
    if (!renderer) {
        at_window_destroy(window);
        at_platform_shutdown();
        return 1;
    }

    scene_assets assets;
    memset(&assets, 0, sizeof(assets));
    bool           assets_ok = create_assets(renderer, &assets);
    at_renderer3d *r3d       = at_renderer3d_create(renderer);
    at_renderer2d *r2d       = at_renderer2d_create(renderer, NULL);
    at_font       *font      = at_font_create_default(renderer);

    if (assets_ok && r3d && r2d && font) {
        at_renderer_set_clear_color(renderer, 0.61f, 0.72f, 0.85f, 1.0f); /* Sky */

        at_camera3d   camera = at_camera3d_make();
        at_fly_camera fly    = at_fly_camera_make();
        reset_camera(&camera, &fly);

        at_camera2d hud_camera    = at_camera2d_make();
        bool        floor_mipmaps = true;
        double      last_time     = at_platform_get_time();
        double      fps_timer     = 0.0;
        uint32_t    frames        = 0;
        uint32_t    fps           = 0;

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
            if (at_window_was_key_pressed(window, AT_KEY_R)) {
                reset_camera(&camera, &fly);
            }
            if (at_window_was_key_pressed(window, AT_KEY_M)) {
                floor_mipmaps = !floor_mipmaps;
            }

            at_fly_camera_update(&fly, &camera, window, dt);

            if (!at_renderer_begin_frame(renderer)) {
                continue;
            }

            uint32_t width  = 0;
            uint32_t height = 0;
            at_renderer_get_size(renderer, &width, &height);

            /* 3D */
            at_renderer3d_begin(r3d, &camera);
            draw_scene(r3d, &assets, (float)now, floor_mipmaps);
            at_renderer3d_end(r3d);

            at_renderer3d_stats stats = at_renderer3d_get_stats(r3d);

            /* 2D on top: labels and HUD */
            at_renderer2d_begin(r2d, &hud_camera);
            draw_labels(r2d, font, &camera, (float)width, (float)height);
            draw_hud(r2d, font, (float)width, (float)height, &stats, &fly, floor_mipmaps, fps);
            at_renderer2d_end(r2d);

            at_renderer_end_frame(renderer);
        }
    }

    /* Destroy in reverse order of creation */
    at_window_set_mouse_captured(window, false);
    at_font_destroy(renderer, font);
    at_renderer2d_destroy(r2d);
    at_renderer3d_destroy(r3d);
    destroy_assets(renderer, &assets);
    at_renderer_destroy(renderer);
    at_window_destroy(window);
    at_platform_shutdown();
    return 0;
}