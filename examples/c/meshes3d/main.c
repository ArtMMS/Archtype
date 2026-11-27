/*
 * 3D meshes example: the engine's primitive meshes (cube, plane and
 * spheres) seen with a free fly camera.
 *   Right mouse button (hold): look around
 *   W A S D:                   move
 *   E or Space / Q or Ctrl:    up / down
 *   Shift (hold):              faster
 *   Mouse wheel:               change the speed
 *   Tab, or 1 to 4:            what the surfaces show (see below)
 *   R:                         reset the camera
 *   Esc:                       quit
 *
 * Views, to check the vertex data of the meshes:
 *   1. Shaded
 *   2. Normals as colors (+X red, +Y green, +Z blue)
 *   3. Texture coordinates: an 8 x 8 checkerboard, red along u, green along v
 *   4. Tangents as colors (they point where u grows)
 *
 * The meshes come from at_mesh_create_*; the shaders are the example's own
 * (the engine's 3D renderer comes in the next part of the roadmap). The HUD
 * is drawn on top with the 2D renderer.
 */

#include <math.h>
#include <stdio.h>

#include <archtype/archtype.h>

#define WINDOW_TITLE "Archtype - Meshes 3D"

/* ------------------------------------------------------------------------ */
/* Data shared with the shaders                                              */
/* ------------------------------------------------------------------------ */

/* Must match "uniform Scene" in the shaders (std140: only mat4 and vec4) */
typedef struct scene_uniforms {
    at_mat4 view_projection;
    float   light_direction[4];
} scene_uniforms;

/* Must match "push_constant Object" in the shaders (96 bytes, max 128) */
typedef struct object_constants {
    at_mat4 model;
    float   color[4];
    float   params[4]; /* x = view mode */
} object_constants;

/* What the surfaces show */
typedef enum view_mode {
    VIEW_SHADED = 0,
    VIEW_NORMALS,
    VIEW_UVS,
    VIEW_TANGENTS,
    VIEW_COUNT
} view_mode;

static const char *const k_view_names[VIEW_COUNT] = {
    "shaded", "normals", "texture coordinates", "tangents",
};

/* ------------------------------------------------------------------------ */
/* Scene                                                                     */
/* ------------------------------------------------------------------------ */

typedef struct scene_meshes {
    at_mesh *floor;
    at_mesh *cube;
    at_mesh *slab;        /* A cube with a different size on each axis */
    at_mesh *low_sphere;  /* Few segments: the facets show            */
    at_mesh *high_sphere; /* Many segments: smooth                    */
} scene_meshes;

static bool create_meshes(at_renderer *renderer, scene_meshes *meshes) {
    meshes->floor       = at_mesh_create_plane(renderer, 40.0f, 40.0f, 20, 20);
    meshes->cube        = at_mesh_create_cube(renderer, at_vec3_make(1.6f, 1.6f, 1.6f));
    meshes->slab        = at_mesh_create_cube(renderer, at_vec3_make(3.0f, 0.4f, 1.2f));
    meshes->low_sphere  = at_mesh_create_sphere(renderer, 1.0f, 10, 6);
    meshes->high_sphere = at_mesh_create_sphere(renderer, 1.0f, 48, 32);

    return meshes->floor && meshes->cube && meshes->slab && meshes->low_sphere &&
           meshes->high_sphere;
}

static void destroy_meshes(at_renderer *renderer, scene_meshes *meshes) {
    at_mesh_destroy(renderer, meshes->high_sphere);
    at_mesh_destroy(renderer, meshes->low_sphere);
    at_mesh_destroy(renderer, meshes->slab);
    at_mesh_destroy(renderer, meshes->cube);
    at_mesh_destroy(renderer, meshes->floor);
}

static void draw_object(at_renderer *renderer, const at_mesh *mesh, at_vec3 position,
                        at_quat rotation, at_color color, view_mode mode) {
    at_transform transform = at_transform_identity();
    transform.position     = position;
    transform.rotation     = rotation;

    object_constants object;
    object.model     = at_transform_to_mat4(transform);
    object.color[0]  = color.r;
    object.color[1]  = color.g;
    object.color[2]  = color.b;
    object.color[3]  = 1.0f;
    object.params[0] = (float)mode;
    object.params[1] = 0.0f;
    object.params[2] = 0.0f;
    object.params[3] = 0.0f;

    at_renderer_push_constants(renderer, &object, sizeof(object));
    at_mesh_draw(renderer, mesh);
}

static void draw_scene(at_renderer *renderer, const scene_meshes *meshes, float time,
                       view_mode mode) {
    const at_quat no_rotation = at_quat_identity();
    const at_quat spin        = at_quat_from_axis_angle(at_vec3_up(), time * 0.4f);

    /* Floor: 20 x 20 squares, the whole texture once */
    draw_object(renderer, meshes->floor, at_vec3_zero(), no_rotation,
                at_color_hex(0x5A6070FF), mode);

    /* A cube tumbling, and a flat slab turning */
    at_quat tumble = at_quat_from_axis_angle(at_vec3_make(0.3f, 1.0f, 0.2f), time * 0.5f);
    draw_object(renderer, meshes->cube, at_vec3_make(-4.5f, 1.6f, 0.0f), tumble,
                at_color_hex(0xF2C14EFF), mode);
    draw_object(renderer, meshes->slab, at_vec3_make(-4.5f, 0.2f, 3.5f), spin,
                at_color_hex(0xF78154FF), mode);

    /* The same sphere with few and with many segments */
    draw_object(renderer, meshes->low_sphere, at_vec3_make(0.0f, 1.4f, 0.0f), spin,
                at_color_hex(0x4D9DE0FF), mode);
    draw_object(renderer, meshes->high_sphere, at_vec3_make(4.5f, 1.4f, 0.0f), spin,
                at_color_hex(0xE15A97FF), mode);
}

/* ------------------------------------------------------------------------ */
/* Camera                                                                    */
/* ------------------------------------------------------------------------ */

static void reset_camera(at_camera3d *camera, at_fly_camera *fly) {
    *camera          = at_camera3d_make();
    camera->position = at_vec3_make(0.0f, 4.0f, 11.0f);
    at_camera3d_look_at(camera, at_vec3_make(0.0f, 1.2f, 0.0f), at_vec3_up());

    float speed = fly->speed; /* Keep the chosen speed */
    *fly        = at_fly_camera_make();
    fly->speed  = speed;
    at_fly_camera_sync(fly, camera);
}

/* ------------------------------------------------------------------------ */
/* HUD                                                                       */
/* ------------------------------------------------------------------------ */

static void draw_hud(at_renderer2d *r2d, const at_font *font, float width, float height,
                     const scene_meshes *meshes, const at_fly_camera *fly, view_mode mode,
                     uint32_t fps) {
    const float scale   = 2.0f;
    const float margin  = 8.0f;
    const float padding = 8.0f;

    char text[512];
    snprintf(text, sizeof(text),
             "%u FPS   view %u: %s (Tab or 1-4)\n"
             "Vertices: cube %u   spheres %u and %u   floor %u\n"
             "\n"
             "Hold right mouse: look   WASD: move   E/Q: up/down\n"
             "Shift: faster   Wheel: speed   R: reset   Esc: quit",
             (unsigned)fps, (unsigned)mode + 1, k_view_names[mode],
             (unsigned)at_mesh_get_vertex_count(meshes->cube),
             (unsigned)at_mesh_get_vertex_count(meshes->low_sphere),
             (unsigned)at_mesh_get_vertex_count(meshes->high_sphere),
             (unsigned)at_mesh_get_vertex_count(meshes->floor));

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

static at_pipeline *create_mesh_pipeline(at_renderer *renderer, at_shader *vertex_shader,
                                         at_shader *fragment_shader) {
    at_pipeline_desc desc = {
        .vertex_shader   = vertex_shader,
        .fragment_shader = fragment_shader,
        .vertex_layout   = at_mesh_vertex_layout(), /* at_vertex3d */
        .binding_count   = 1,
        .bindings        = {
            { .binding = 0, .type = AT_BINDING_UNIFORM_BUFFER }, /* Vertex and fragment */
        },
        .push_constant_size = sizeof(object_constants),
        .cull_mode          = AT_CULL_BACK, /* Closed meshes; the floor is seen from above */
        .depth_test         = true,
        .depth_write        = true,
    };
    return at_pipeline_create(renderer, &desc);
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
        .app_name          = "Meshes 3D Example",
        .enable_validation = true,
        .vsync             = true,
    };

    at_renderer *renderer = at_renderer_create(&renderer_desc);
    if (!renderer) {
        at_window_destroy(window);
        at_platform_shutdown();
        return 1;
    }

    /* Meshes and the scene uniform buffer */
    scene_meshes meshes = { 0 };
    bool         meshes_ok = create_meshes(renderer, &meshes);

    at_buffer_desc scene_desc = {
        .type = AT_BUFFER_UNIFORM,
        .size = sizeof(scene_uniforms),
    };
    at_buffer *scene_buffer = at_buffer_create(renderer, &scene_desc);

    /* Pipeline */
    at_shader *vertex_shader   = at_shader_load(renderer, "shaders/meshes3d.vert.spv");
    at_shader *fragment_shader = at_shader_load(renderer, "shaders/meshes3d.frag.spv");

    at_pipeline *pipeline = NULL;
    if (vertex_shader && fragment_shader) {
        pipeline = create_mesh_pipeline(renderer, vertex_shader, fragment_shader);
    }

    at_shader_destroy(renderer, vertex_shader);
    at_shader_destroy(renderer, fragment_shader);

    /* HUD */
    at_renderer2d *r2d  = at_renderer2d_create(renderer, NULL);
    at_font       *font = at_font_create_default(renderer);

    if (meshes_ok && scene_buffer && pipeline && r2d && font) {
        at_renderer_set_clear_color(renderer, 0.12f, 0.13f, 0.17f, 1.0f);

        at_camera3d   camera = at_camera3d_make();
        at_fly_camera fly    = at_fly_camera_make();
        reset_camera(&camera, &fly);

        at_camera2d hud_camera = at_camera2d_make();
        view_mode   mode       = VIEW_SHADED;
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
                mode = (view_mode)((mode + 1) % VIEW_COUNT);
            }
            const at_key mode_keys[VIEW_COUNT] = { AT_KEY_1, AT_KEY_2, AT_KEY_3, AT_KEY_4 };
            for (int i = 0; i < VIEW_COUNT; ++i) {
                if (at_window_was_key_pressed(window, mode_keys[i])) {
                    mode = (view_mode)i;
                }
            }

            at_fly_camera_update(&fly, &camera, window, dt);

            if (!at_renderer_begin_frame(renderer)) {
                continue;
            }

            uint32_t width  = 0;
            uint32_t height = 0;
            at_renderer_get_size(renderer, &width, &height);

            /* Scene constants: once per frame */
            scene_uniforms scene;
            scene.view_projection = at_camera3d_view_projection(&camera, (float)width,
                                                                (float)height);

            at_vec3 light = at_vec3_normalize(at_vec3_make(-0.4f, -1.0f, -0.3f));
            scene.light_direction[0] = light.x;
            scene.light_direction[1] = light.y;
            scene.light_direction[2] = light.z;
            scene.light_direction[3] = 0.0f;

            at_buffer_update(renderer, scene_buffer, &scene, sizeof(scene), 0);

            /* 3D: the meshes (each at_mesh_draw binds its own buffers) */
            at_renderer_bind_pipeline(renderer, pipeline);
            at_renderer_bind_uniform_buffer(renderer, 0, scene_buffer);
            draw_scene(renderer, &meshes, (float)now, mode);

            /* 2D on top */
            at_renderer2d_begin(r2d, &hud_camera);
            draw_hud(r2d, font, (float)width, (float)height, &meshes, &fly, mode, fps);
            at_renderer2d_end(r2d);

            at_renderer_end_frame(renderer);
        }
    }

    /* Destroy in reverse order of creation */
    at_window_set_mouse_captured(window, false);
    at_font_destroy(renderer, font);
    at_renderer2d_destroy(r2d);
    at_pipeline_destroy(renderer, pipeline);
    at_buffer_destroy(renderer, scene_buffer);
    destroy_meshes(renderer, &meshes);
    at_renderer_destroy(renderer);
    at_window_destroy(window);
    at_platform_shutdown();
    return 0;
}