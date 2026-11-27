/*
 * 3D models example: loads an OBJ model (with its MTL materials) and shows
 * it on a floor, with the camera placed to fit it.
 *
 *   models3d_c                  shows models/crate.obj (next to the program)
 *   models3d_c path/to/file.obj shows any OBJ file
 *
 * Relative paths are resolved from the executable's folder, like every
 * asset; for a file somewhere else, pass its full path.
 *
 *   Right mouse button (hold): look around
 *   W A S D:                   move
 *   E or Space / Q or Ctrl:    up / down
 *   Shift (hold):              faster
 *   Mouse wheel:               change the speed
 *   T:                         turntable (spin the model) on / off
 *   R:                         reset the camera
 *   Esc:                       quit
 */

#include <math.h>
#include <stdio.h>

#include <archtype/archtype.h>

#define WINDOW_TITLE  "Archtype - Models 3D"
#define DEFAULT_MODEL "models/crate.obj"

/* Where the model is and how big, to place it and the camera */
typedef struct model_frame {
    at_vec3 center; /* Of its bounding box, in its own space */
    float   bottom; /* Lowest Y, in its own space            */
    float   radius; /* Half the bounding box's diagonal      */
} model_frame;

static model_frame measure_model(const at_model *model) {
    model_frame frame = { at_vec3_zero(), 0.0f, 1.0f };
    if (!model) {
        return frame;
    }

    at_vec3 min, max;
    at_model_get_bounds(model, &min, &max);
    frame.center = at_vec3_scale(at_vec3_add(min, max), 0.5f);
    frame.bottom = min.y;
    frame.radius = at_maxf(at_vec3_length(at_vec3_sub(max, min)) * 0.5f, 0.01f);
    return frame;
}

/* Stands the model on the floor at the origin, turned by angle */
static at_mat4 model_transform(const model_frame *frame, float angle) {
    at_mat4 to_origin = at_mat4_translate(at_vec3_make(-frame->center.x, -frame->bottom,
                                                       -frame->center.z));
    return at_mat4_mul(at_mat4_rotate(at_vec3_up(), angle), to_origin);
}

/* Looks at the model from the front, a bit from above, far enough to see */
/* all of it                                                              */
static void reset_camera(at_camera3d *camera, at_fly_camera *fly, const model_frame *frame) {
    *camera        = at_camera3d_make();
    camera->near_z = frame->radius * 0.01f;
    camera->far_z  = frame->radius * 200.0f;

    float   height   = frame->center.y - frame->bottom;
    at_vec3 target   = at_vec3_make(0.0f, height, 0.0f);
    float   distance = frame->radius / sinf(camera->fov_y * 0.5f) * 1.1f;
    at_vec3 view     = at_vec3_normalize(at_vec3_make(0.6f, 0.45f, 1.0f));

    camera->position = at_vec3_add(target, at_vec3_scale(view, distance));
    at_camera3d_look_at(camera, target, at_vec3_up());

    *fly           = at_fly_camera_make();
    fly->speed     = frame->radius * 1.5f;
    fly->min_speed = frame->radius * 0.05f;
    fly->max_speed = frame->radius * 50.0f;
    at_fly_camera_sync(fly, camera);
}

static void add_lights(at_renderer3d *r3d) {
    /* A warm key light from the front-right and a dim cool one from behind */
    at_directional_light key  = at_directional_light_make(at_vec3_make(-0.5f, -1.0f, -0.6f),
                                                          at_color_hex(0xFFF2E0FF), 1.0f);
    at_directional_light fill = at_directional_light_make(at_vec3_make(0.6f, -0.4f, 0.8f),
                                                          at_color_hex(0x9DB4FFFF), 0.35f);
    at_renderer3d_add_directional_light(r3d, &key);
    at_renderer3d_add_directional_light(r3d, &fill);
}

static void draw_hud(at_renderer2d *r2d, const at_font *font, float width, float height,
                     const char *path, const at_model *model,
                     const at_renderer3d_stats *stats, const at_fly_camera *fly,
                     bool turntable, uint32_t fps) {
    const float scale   = 2.0f;
    const float margin  = 8.0f;
    const float padding = 8.0f;

    char text[1024];
    if (model) {
        snprintf(text, sizeof(text),
                 "%u FPS   %u draw calls   %u triangles\n"
                 "%s\n"
                 "%u parts, %u materials\n"
                 "\n"
                 "T: turntable %s   R: reset camera\n"
                 "Hold right mouse: look   WASD: move   E/Q: up/down\n"
                 "Shift: faster   Wheel: speed   Esc: quit",
                 (unsigned)fps, (unsigned)stats->draw_calls, (unsigned)stats->triangles, path,
                 (unsigned)at_model_get_part_count(model),
                 (unsigned)at_model_get_material_count(model), turntable ? "ON" : "OFF");
    } else {
        snprintf(text, sizeof(text),
                 "Could not load the model:\n"
                 "%s\n"
                 "\n"
                 "See the terminal for why. Esc: quit",
                 path);
    }

    at_vec2 size = at_font_measure(font, text, scale);
    at_vec2 box  = at_vec2_make(size.x + 2.0f * padding, size.y + 2.0f * padding);
    float   left = -width * 0.5f + margin;
    float   top  =  height * 0.5f - margin;

    at_renderer2d_draw_rect(r2d, at_vec2_make(left + box.x * 0.5f, top - box.y * 0.5f), box,
                            0.0f, at_color_rgba(0.0f, 0.0f, 0.0f, 0.55f));
    at_renderer2d_draw_text(r2d, font, text, at_vec2_make(left + padding, top - padding),
                            scale, model ? at_color_white() : at_color_hex(0xFF8080FF));

    /* Crosshair while looking around */
    if (fly->looking) {
        at_renderer2d_draw_rect(r2d, at_vec2_zero(), at_vec2_make(12.0f, 2.0f), 0.0f,
                                at_color_white());
        at_renderer2d_draw_rect(r2d, at_vec2_zero(), at_vec2_make(2.0f, 12.0f), 0.0f,
                                at_color_white());
    }
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : DEFAULT_MODEL;

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
        .app_name          = "Models 3D Example",
        .enable_validation = true,
        .vsync             = true,
    };

    at_renderer *renderer = at_renderer_create(&renderer_desc);
    if (!renderer) {
        at_window_destroy(window);
        at_platform_shutdown();
        return 1;
    }

    /* The model may fail to load: the example still runs and says so */
    at_model      *model = at_model_load(renderer, path);
    at_mesh       *floor = at_mesh_create_plane(renderer, 1.0f, 1.0f, 1, 1);
    at_renderer3d *r3d   = at_renderer3d_create(renderer);
    at_renderer2d *r2d   = at_renderer2d_create(renderer, NULL);
    at_font       *font  = at_font_create_default(renderer);

    if (floor && r3d && r2d && font) {
        at_renderer_set_clear_color(renderer, 0.42f, 0.48f, 0.56f, 1.0f);

        model_frame   frame  = measure_model(model);
        at_camera3d   camera = at_camera3d_make();
        at_fly_camera fly    = at_fly_camera_make();
        reset_camera(&camera, &fly, &frame);

        /* The floor: a big plane, sized to the model */
        float       floor_size     = frame.radius * 8.0f;
        at_material floor_material = at_material_make();
        floor_material.base_color  = at_color_hex(0x8A8F96FF);
        floor_material.specular    = 0.1f;

        bool        turntable  = false;
        float       angle      = 0.0f;
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
                reset_camera(&camera, &fly, &frame);
            }
            if (at_window_was_key_pressed(window, AT_KEY_T)) {
                turntable = !turntable;
            }
            if (turntable) {
                angle += dt * 0.6f;
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
            add_lights(r3d);
            at_renderer3d_draw_mesh_ex(r3d, floor,
                                       at_mat4_scale(at_vec3_make(floor_size, 1.0f, floor_size)),
                                       &floor_material);
            at_model_draw(r3d, model, model_transform(&frame, angle));
            at_renderer3d_end(r3d);

            at_renderer3d_stats stats = at_renderer3d_get_stats(r3d);

            /* 2D on top */
            at_renderer2d_begin(r2d, &hud_camera);
            draw_hud(r2d, font, (float)width, (float)height, path, model, &stats, &fly,
                     turntable, fps);
            at_renderer2d_end(r2d);

            at_renderer_end_frame(renderer);
        }
    }

    /* Destroy in reverse order of creation */
    at_window_set_mouse_captured(window, false);
    at_font_destroy(renderer, font);
    at_renderer2d_destroy(r2d);
    at_renderer3d_destroy(r3d);
    at_mesh_destroy(renderer, floor);
    at_model_destroy(renderer, model);
    at_renderer_destroy(renderer);
    at_window_destroy(window);
    at_platform_shutdown();
    return 0;
}