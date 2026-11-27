/*
 * 3D lights example: directional, point and spot lights (Blinn-Phong) on a
 * night scene, with the engine's 3D renderer.
 *   Right mouse button (hold): look around
 *   W A S D:                   move
 *   E or Space / Q or Ctrl:    up / down
 *   Shift (hold):              faster
 *   Mouse wheel:               change the speed
 *   1:                         moonlight (directional light) on / off
 *   2:                         orbiting point lights on / off
 *   - and =:                   fewer / more point lights
 *   3:                         flashlight (spot light on the camera) on / off
 *   4:                         stage light (spot light from above) on / off
 *   R:                         reset the camera
 *   Esc:                       quit
 *
 * Each point light is shown as a small glowing sphere (an unlit, emissive
 * material): spheres don't give off light themselves, the lights do.
 */

#include <math.h>
#include <stdio.h>

#include <archtype/archtype.h>

#define WINDOW_TITLE "Archtype - Lights 3D"

#define PILLAR_COUNT  12
#define PILLAR_RADIUS 9.0f

/* ------------------------------------------------------------------------ */
/* Scene                                                                     */
/* ------------------------------------------------------------------------ */

typedef struct scene_meshes {
    at_mesh *floor;
    at_mesh *cube;   /* 1 x 1 x 1 */
    at_mesh *sphere; /* Radius 1  */
} scene_meshes;

typedef struct light_switches {
    bool     moon;
    bool     points;
    bool     flashlight;
    bool     stage;
    uint32_t point_count;
} light_switches;

static bool create_meshes(at_renderer *renderer, scene_meshes *meshes) {
    meshes->floor  = at_mesh_create_plane(renderer, 50.0f, 50.0f, 1, 1);
    meshes->cube   = at_mesh_create_cube(renderer, at_vec3_one());
    meshes->sphere = at_mesh_create_sphere(renderer, 1.0f, 48, 24);
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

static at_color hue_color(float hue) {
    return at_color_rgb(0.5f + 0.5f * cosf(AT_TAU * hue),
                        0.5f + 0.5f * cosf(AT_TAU * (hue + 0.33f)),
                        0.5f + 0.5f * cosf(AT_TAU * (hue + 0.66f)));
}

/* Where point light i is at a time: circling the middle, bobbing up and down */
static at_vec3 orbiter_position(uint32_t i, uint32_t count, float time) {
    float angle  = time * 0.5f + AT_TAU * (float)i / (float)count;
    float radius = 4.5f + 1.5f * sinf((float)i * 1.7f);
    return at_vec3_make(cosf(angle) * radius, 1.0f + 0.6f * sinf(time * 1.3f + (float)i),
                        sinf(angle) * radius);
}

static void draw_world(at_renderer3d *r3d, const scene_meshes *meshes, float time) {
    /* A slightly shiny floor: the lights leave highlights on it */
    at_material floor = at_material_make();
    floor.base_color  = at_color_hex(0x707480FF);
    floor.specular    = 0.4f;
    floor.shininess   = 48.0f;
    at_renderer3d_draw_mesh_ex(r3d, meshes->floor, at_mat4_identity(), &floor);

    /* A ring of tall pillars */
    at_material stone = at_material_make();
    stone.base_color  = at_color_hex(0xC8C4BCFF);
    stone.specular    = 0.2f;
    for (int i = 0; i < PILLAR_COUNT; ++i) {
        float angle = AT_TAU * (float)i / PILLAR_COUNT;
        at_renderer3d_draw_mesh_ex(r3d, meshes->cube,
                                   make_model(at_vec3_make(cosf(angle) * PILLAR_RADIUS, 2.0f,
                                                           sinf(angle) * PILLAR_RADIUS),
                                              at_quat_from_axis_angle(at_vec3_up(), -angle),
                                              at_vec3_make(1.0f, 4.0f, 1.0f)),
                                   &stone);
    }

    /* Shiny spheres and a turning block in the middle */
    const at_color colors[3] = {
        at_color_hex(0xE8E8E8FF), at_color_hex(0xC8463CFF), at_color_hex(0x4D9DE0FF),
    };
    for (int i = 0; i < 3; ++i) {
        float angle = AT_TAU * (float)i / 3.0f;

        at_material shiny = at_material_make();
        shiny.base_color  = colors[i];
        shiny.specular    = 1.0f;
        shiny.shininess   = 64.0f;
        at_renderer3d_draw_mesh_ex(r3d, meshes->sphere,
                                   make_model(at_vec3_make(cosf(angle) * 2.2f, 1.0f,
                                                           sinf(angle) * 2.2f),
                                              at_quat_identity(), at_vec3_one()),
                                   &shiny);
    }

    at_material block = at_material_make();
    block.base_color  = at_color_hex(0xF2C14EFF);
    at_renderer3d_draw_mesh_ex(r3d, meshes->cube,
                               make_model(at_vec3_make(0.0f, 0.75f, 0.0f),
                                          at_quat_from_axis_angle(at_vec3_up(), time * 0.3f),
                                          at_vec3_make(1.2f, 1.5f, 1.2f)),
                               &block);
}

static void add_lights(at_renderer3d *r3d, const scene_meshes *meshes,
                       const light_switches *switches, const at_camera3d *camera, float time) {
    /* 1. Moonlight: dim, bluish, from above and to the side */
    if (switches->moon) {
        at_directional_light moon = at_directional_light_make(at_vec3_make(0.3f, -1.0f, 0.5f),
                                                              at_color_hex(0x9CB4FFFF), 0.35f);
        at_renderer3d_add_directional_light(r3d, &moon);
    }

    /* 2. Colored point lights circling the middle, each with a small */
    /*    glowing sphere where it is                                   */
    if (switches->points) {
        for (uint32_t i = 0; i < switches->point_count; ++i) {
            at_vec3  position = orbiter_position(i, switches->point_count, time);
            at_color color    = hue_color((float)i / (float)switches->point_count);

            at_point_light light = at_point_light_make(position, color, 6.0f, 8.0f);
            at_renderer3d_add_point_light(r3d, &light);

            at_material marker = at_material_make();
            marker.base_color  = color;
            marker.unlit       = true;
            at_renderer3d_draw_mesh_ex(r3d, meshes->sphere,
                                       make_model(position, at_quat_identity(),
                                                  at_vec3_make(0.12f, 0.12f, 0.12f)),
                                       &marker);
        }
    }

    /* 3. Flashlight: a narrow warm cone where the camera looks */
    if (switches->flashlight) {
        at_spot_light flashlight = at_spot_light_make(camera->position,
                                                      at_camera3d_forward(camera),
                                                      at_color_hex(0xFFF0D8FF), 25.0f, 30.0f);
        flashlight.inner_angle = at_radians(10.0f);
        flashlight.outer_angle = at_radians(16.0f);
        at_renderer3d_add_spot_light(r3d, &flashlight);
    }

    /* 4. Stage light: a wide cone from high above, swaying slowly */
    if (switches->stage) {
        at_vec3 direction = at_vec3_make(0.35f * cosf(time * 0.7f), -1.0f,
                                         0.35f * sinf(time * 0.7f));
        at_spot_light stage = at_spot_light_make(at_vec3_make(0.0f, 10.0f, 0.0f), direction,
                                                 at_color_hex(0xFFD0E8FF), 80.0f, 16.0f);
        stage.inner_angle = at_radians(14.0f);
        stage.outer_angle = at_radians(22.0f);
        at_renderer3d_add_spot_light(r3d, &stage);
    }
}

/* ------------------------------------------------------------------------ */
/* Camera                                                                    */
/* ------------------------------------------------------------------------ */

static void reset_camera(at_camera3d *camera, at_fly_camera *fly) {
    *camera          = at_camera3d_make();
    camera->position = at_vec3_make(0.0f, 6.0f, 15.0f);
    at_camera3d_look_at(camera, at_vec3_make(0.0f, 1.0f, 0.0f), at_vec3_up());

    float speed = fly->speed; /* Keep the chosen speed */
    *fly        = at_fly_camera_make();
    fly->speed  = speed;
    at_fly_camera_sync(fly, camera);
}

/* ------------------------------------------------------------------------ */
/* HUD                                                                       */
/* ------------------------------------------------------------------------ */

static const char *on_off(bool value) {
    return value ? "ON" : "OFF";
}

static void draw_hud(at_renderer2d *r2d, const at_font *font, float width, float height,
                     const at_renderer3d_stats *stats, const at_fly_camera *fly,
                     const light_switches *switches, uint32_t fps) {
    const float scale   = 2.0f;
    const float margin  = 8.0f;
    const float padding = 8.0f;

    char text[640];
    snprintf(text, sizeof(text),
             "%u FPS   %u draw calls   %u lights\n"
             "1: moonlight %s\n"
             "2: point lights %s (%u, - and = to change)\n"
             "3: flashlight %s\n"
             "4: stage light %s\n"
             "\n"
             "Hold right mouse: look   WASD: move   E/Q: up/down\n"
             "Shift: faster   Wheel: speed   R: reset   Esc: quit",
             (unsigned)fps, (unsigned)stats->draw_calls, (unsigned)stats->lights,
             on_off(switches->moon), on_off(switches->points),
             (unsigned)switches->point_count, on_off(switches->flashlight),
             on_off(switches->stage));

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

static void handle_keys(at_window *window, light_switches *switches) {
    if (at_window_was_key_pressed(window, AT_KEY_1)) {
        switches->moon = !switches->moon;
    }
    if (at_window_was_key_pressed(window, AT_KEY_2)) {
        switches->points = !switches->points;
    }
    if (at_window_was_key_pressed(window, AT_KEY_3)) {
        switches->flashlight = !switches->flashlight;
    }
    if (at_window_was_key_pressed(window, AT_KEY_4)) {
        switches->stage = !switches->stage;
    }
    if (at_window_was_key_pressed(window, AT_KEY_MINUS) && switches->point_count > 0) {
        switches->point_count--;
    }
    if (at_window_was_key_pressed(window, AT_KEY_EQUALS) &&
        switches->point_count < AT_RENDERER3D_MAX_POINT_LIGHTS) {
        switches->point_count++;
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

    at_renderer_desc renderer_desc = {
        .window            = window,
        .app_name          = "Lights 3D Example",
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
        /* Night: a dark blue sky and a dim ambient light */
        at_renderer_set_clear_color(renderer, 0.03f, 0.04f, 0.08f, 1.0f);
        at_renderer3d_set_ambient(r3d, at_color_hex(0x283050FF), at_color_hex(0x141418FF),
                                  0.5f);

        at_camera3d   camera = at_camera3d_make();
        at_fly_camera fly    = at_fly_camera_make();
        reset_camera(&camera, &fly);

        light_switches switches = { true, true, false, true, 8 };

        at_camera2d hud_camera = at_camera2d_make();
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
            handle_keys(window, &switches);

            at_fly_camera_update(&fly, &camera, window, dt);

            if (!at_renderer_begin_frame(renderer)) {
                continue;
            }

            uint32_t width  = 0;
            uint32_t height = 0;
            at_renderer_get_size(renderer, &width, &height);

            /* 3D: lights and meshes, in any order, between begin and end */
            at_renderer3d_begin(r3d, &camera);
            add_lights(r3d, &meshes, &switches, &camera, (float)now);
            draw_world(r3d, &meshes, (float)now);
            at_renderer3d_end(r3d);

            at_renderer3d_stats stats = at_renderer3d_get_stats(r3d);

            /* 2D on top */
            at_renderer2d_begin(r2d, &hud_camera);
            draw_hud(r2d, font, (float)width, (float)height, &stats, &fly, &switches, fps);
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