/*
 * 3D renderer example: a scene drawn with the engine's 3D renderer
 * (at_renderer3d). The example has no shaders or pipelines of its own,
 * only meshes, transforms and colors.
 *   Right mouse button (hold): look around
 *   W A S D:                   move
 *   E or Space / Q or Ctrl:    up / down
 *   Shift (hold):              faster
 *   Mouse wheel:               change the speed
 *   Tab:                       add or remove a crowd of 2500 cubes
 *   R:                         reset the camera
 *   Esc:                       quit
 *
 * The squashed spheres in the middle check the normals of objects scaled
 * differently on each axis: their light must follow their surface. The
 * HUD shows the draw calls and triangles of the frame.
 */

#include <math.h>
#include <stdio.h>

#include <archtype/archtype.h>

#define WINDOW_TITLE "Archtype - Scene 3D"

#define GRID_SIZE     11   /* Pillars per side            */
#define GRID_SPACING  4.0f /* World units between pillars */
#define CROWD_SIZE    50   /* Crowd cubes per side        */
#define CROWD_SPACING 1.2f

/* ------------------------------------------------------------------------ */
/* Scene                                                                     */
/* ------------------------------------------------------------------------ */

typedef struct scene_meshes {
    at_mesh *floor;
    at_mesh *cube;   /* 1 x 1 x 1: transforms give the sizes */
    at_mesh *sphere; /* Radius 1                             */
} scene_meshes;

static bool create_meshes(at_renderer *renderer, scene_meshes *meshes) {
    meshes->floor  = at_mesh_create_plane(renderer, 60.0f, 60.0f, 30, 30);
    meshes->cube   = at_mesh_create_cube(renderer, at_vec3_one());
    meshes->sphere = at_mesh_create_sphere(renderer, 1.0f, 32, 16);
    return meshes->floor && meshes->cube && meshes->sphere;
}

static void destroy_meshes(at_renderer *renderer, scene_meshes *meshes) {
    at_mesh_destroy(renderer, meshes->sphere);
    at_mesh_destroy(renderer, meshes->cube);
    at_mesh_destroy(renderer, meshes->floor);
}

static at_mat4 make_model(at_vec3 position, at_quat rotation, at_vec3 scale) {
    at_transform transform = at_transform_identity();
    transform.position     = position;
    transform.rotation     = rotation;
    transform.scale        = scale;
    return at_transform_to_mat4(transform);
}

/* A field of pillars with different heights, around the middle */
static void draw_pillars(at_renderer3d *r3d, const scene_meshes *meshes) {
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

            float   height   = 0.5f + 5.0f * (0.5f + 0.5f * sinf((float)x * 0.7f)
                                                          * cosf((float)z * 0.55f));
            at_vec3 position = at_vec3_make((float)x * GRID_SPACING, height * 0.5f,
                                            (float)z * GRID_SPACING);

            at_renderer3d_draw_mesh(r3d, meshes->cube,
                                    make_model(position, at_quat_identity(),
                                               at_vec3_make(1.2f, height, 1.2f)),
                                    palette[(x + z + 2 * GRID_SIZE) % 5]);
        }
    }
}

/* Spheres orbiting the middle, and squashed spheres turning in it */
static void draw_center(at_renderer3d *r3d, const scene_meshes *meshes, float time) {
    const at_color colors[3] = {
        at_color_hex(0xF2C14EFF), at_color_hex(0x7DCE82FF), at_color_hex(0x4D9DE0FF),
    };

    for (int i = 0; i < 3; ++i) {
        float   angle    = time * 0.6f + AT_TAU * (float)i / 3.0f;
        at_vec3 position = at_vec3_make(cosf(angle) * 3.0f,
                                        1.4f + 0.4f * sinf(time * 2.0f + (float)i),
                                        sinf(angle) * 3.0f);
        at_renderer3d_draw_mesh(r3d, meshes->sphere,
                                make_model(position, at_quat_identity(),
                                           at_vec3_make(0.6f, 0.6f, 0.6f)),
                                colors[i]);
    }

    /* Flat and wide, then tall and thin: the light must still follow them */
    at_quat turn = at_quat_from_axis_angle(at_vec3_up(), time * 0.5f);
    at_renderer3d_draw_mesh(r3d, meshes->sphere,
                            make_model(at_vec3_make(0.0f, 0.6f, 0.0f), turn,
                                       at_vec3_make(1.6f, 0.5f, 0.9f)),
                            at_color_hex(0xE15A97FF));
    at_renderer3d_draw_mesh(r3d, meshes->sphere,
                            make_model(at_vec3_make(0.0f, 2.4f, 0.0f), turn,
                                       at_vec3_make(0.35f, 1.2f, 0.35f)),
                            at_color_hex(0xF78154FF));
}

/* A floating grid of small cubes, each a mesh of its own */
static void draw_crowd(at_renderer3d *r3d, const scene_meshes *meshes, float time) {
    const float extent = (float)(CROWD_SIZE - 1) * CROWD_SPACING * 0.5f;

    for (int z = 0; z < CROWD_SIZE; ++z) {
        for (int x = 0; x < CROWD_SIZE; ++x) {
            at_vec3 position = at_vec3_make(-extent + (float)x * CROWD_SPACING,
                                            12.0f + 0.6f * sinf(time * 1.5f + (float)x * 0.3f
                                                                + (float)z * 0.2f),
                                            -extent + (float)z * CROWD_SPACING);
            at_quat rotation = at_quat_from_axis_angle(at_vec3_make(1.0f, 1.0f, 0.0f),
                                                       time + (float)(x + z) * 0.1f);

            float    hue   = (float)(x + z) / (float)(2 * CROWD_SIZE);
            at_color color = at_color_rgb(0.5f + 0.5f * cosf(AT_TAU * hue),
                                          0.5f + 0.5f * cosf(AT_TAU * (hue + 0.33f)),
                                          0.5f + 0.5f * cosf(AT_TAU * (hue + 0.66f)));

            at_renderer3d_draw_mesh(r3d, meshes->cube,
                                    make_model(position, rotation,
                                               at_vec3_make(0.5f, 0.5f, 0.5f)),
                                    color);
        }
    }
}

static void draw_scene(at_renderer3d *r3d, const scene_meshes *meshes, float time,
                       bool crowd) {
    at_renderer3d_draw_mesh(r3d, meshes->floor, at_mat4_identity(), at_color_hex(0x5A6070FF));
    draw_pillars(r3d, meshes);
    draw_center(r3d, meshes, time);
    if (crowd) {
        draw_crowd(r3d, meshes, time);
    }
}

/* ------------------------------------------------------------------------ */
/* Camera                                                                    */
/* ------------------------------------------------------------------------ */

static void reset_camera(at_camera3d *camera, at_fly_camera *fly) {
    *camera          = at_camera3d_make();
    camera->position = at_vec3_make(0.0f, 8.0f, 24.0f);
    at_camera3d_look_at(camera, at_vec3_make(0.0f, 2.0f, 0.0f), at_vec3_up());

    float speed = fly->speed; /* Keep the chosen speed */
    *fly        = at_fly_camera_make();
    fly->speed  = speed;
    at_fly_camera_sync(fly, camera);
}

/* ------------------------------------------------------------------------ */
/* HUD                                                                       */
/* ------------------------------------------------------------------------ */

static void draw_hud(at_renderer2d *r2d, const at_font *font, float width, float height,
                     const at_renderer3d_stats *stats, const at_fly_camera *fly, bool crowd,
                     uint32_t fps) {
    const float scale   = 2.0f;
    const float margin  = 8.0f;
    const float padding = 8.0f;

    char text[512];
    snprintf(text, sizeof(text),
             "%u FPS   %u draw calls   %u triangles\n"
             "Crowd of %u cubes: %s (Tab)\n"
             "\n"
             "Hold right mouse: look   WASD: move   E/Q: up/down\n"
             "Shift: faster   Wheel: speed   R: reset   Esc: quit",
             (unsigned)fps, (unsigned)stats->draw_calls, (unsigned)stats->triangles,
             (unsigned)(CROWD_SIZE * CROWD_SIZE), crowd ? "ON" : "OFF");

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
        .app_name          = "Scene 3D Example",
        .enable_validation = true,
        .vsync             = true,
    };

    at_renderer *renderer = at_renderer_create(&renderer_desc);
    if (!renderer) {
        at_window_destroy(window);
        at_platform_shutdown();
        return 1;
    }

    scene_meshes   meshes    = { 0 };
    bool           meshes_ok = create_meshes(renderer, &meshes);
    at_renderer3d *r3d       = at_renderer3d_create(renderer);
    at_renderer2d *r2d       = at_renderer2d_create(renderer, NULL);
    at_font       *font      = at_font_create_default(renderer);

    if (meshes_ok && r3d && r2d && font) {
        at_renderer_set_clear_color(renderer, 0.61f, 0.72f, 0.85f, 1.0f); /* Sky */

        at_camera3d   camera = at_camera3d_make();
        at_fly_camera fly    = at_fly_camera_make();
        reset_camera(&camera, &fly);

        at_camera2d hud_camera = at_camera2d_make();
        bool        crowd      = false;
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
            if (at_window_was_key_pressed(window, AT_KEY_R)) {
                reset_camera(&camera, &fly);
            }
            if (at_window_was_key_pressed(window, AT_KEY_TAB)) {
                crowd = !crowd;
            }

            at_fly_camera_update(&fly, &camera, window, dt);

            if (!at_renderer_begin_frame(renderer)) {
                continue;
            }

            uint32_t width  = 0;
            uint32_t height = 0;
            at_renderer_get_size(renderer, &width, &height);

            /* 3D: no shaders or pipelines here, only meshes and transforms */
            at_renderer3d_begin(r3d, &camera);
            draw_scene(r3d, &meshes, (float)now, crowd);
            at_renderer3d_end(r3d);

            at_renderer3d_stats stats = at_renderer3d_get_stats(r3d);

            /* 2D on top */
            at_renderer2d_begin(r2d, &hud_camera);
            draw_hud(r2d, font, (float)width, (float)height, &stats, &fly, crowd, fps);
            at_renderer2d_end(r2d);

            at_renderer_end_frame(renderer);
        }
    }

    /* Destroy in reverse order of creation */
    at_window_set_mouse_captured(window, false);
    at_font_destroy(renderer, font);
    at_renderer2d_destroy(r2d);
    at_renderer3d_destroy(r3d);
    destroy_meshes(renderer, &meshes);
    at_renderer_destroy(renderer);
    at_window_destroy(window);
    at_platform_shutdown();
    return 0;
}