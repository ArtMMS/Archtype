#include "archtype/renderer/renderer3d.h"
#include "renderer/renderer_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SPIR-V of the 3D shaders, compiled from src/renderer/shaders/ by CMake */
/* (archtype_embed_shaders), as C arrays.                                 */
static const uint32_t k_vertex_shader[] =
#include "embedded/renderer3d.vert.inc"
;

static const uint32_t k_fragment_shader[] =
#include "embedded/renderer3d.frag.inc"
;

#define AT_RENDERER3D_INITIAL_ITEMS 256

/* One pipeline per combination of these bits */
#define AT_PIPELINE3D_DOUBLE_SIDED 1u
#define AT_PIPELINE3D_TRANSPARENT  2u
#define AT_PIPELINE3D_COUNT        4u

/* Lights as the shaders read them: colors already linear and multiplied */
/* by the intensity, directions normalized.                              */
typedef struct at_gpu_directional_light {
    float direction[4]; /* xyz: where the light travels */
    float color[4];     /* rgb                          */
} at_gpu_directional_light;

typedef struct at_gpu_point_light {
    float position_range[4]; /* xyz: position, w: range */
    float color[4];          /* rgb                     */
} at_gpu_point_light;

typedef struct at_gpu_spot_light {
    float position_range[4];      /* xyz: position, w: range                      */
    float direction_cos_outer[4]; /* xyz: where it points, w: cos(outer angle)    */
    float color_cos_inner[4];     /* rgb: color, w: cos(inner angle)              */
} at_gpu_spot_light;

/* Set 0, binding 0: what every mesh of a pass shares. Must match "uniform */
/* Frame" in renderer3d.frag (std140: every member is a vec4, a mat4 or an */
/* array of structs of vec4, so there is no padding to add).               */
typedef struct at_frame_uniforms3d {
    at_mat4                  view_projection;
    float                    camera_position[4]; /* xyz                        */
    float                    ambient_sky[4];     /* rgb: light on faces up     */
    float                    ambient_ground[4];  /* rgb: light on faces down   */
    uint32_t                 light_counts[4];    /* directional, point, spot   */
    at_gpu_directional_light directional[AT_RENDERER3D_MAX_DIRECTIONAL_LIGHTS];
    at_gpu_point_light       point[AT_RENDERER3D_MAX_POINT_LIGHTS];
    at_gpu_spot_light        spot[AT_RENDERER3D_MAX_SPOT_LIGHTS];
} at_frame_uniforms3d;

/* Per mesh, in push constants: 128 bytes, the most every GPU allows.   */
/* Must match "push_constant Object" in the 3D shaders. The material's  */
/* texture goes in set 1.                                               */
typedef struct at_object_constants3d {
    at_mat4 model;
    float   base_color[4];   /* sRGB rgb, alpha                              */
    float   emissive[4];     /* sRGB rgb, w = strength                       */
    float   params[4];       /* specular, shininess, alpha cutoff, unlit     */
    float   uv_transform[4]; /* xy = scale, zw = offset                      */
} at_object_constants3d;

/* One mesh waiting for end */
typedef struct at_draw_item3d {
    const at_mesh *mesh;
    at_mat4        model;
    at_material    material;
    uint32_t       pipeline; /* AT_PIPELINE3D_* bits                     */
    uint32_t       sequence; /* Order it was drawn in: keeps ties in order */
    float          distance; /* From the camera, squared                  */
} at_draw_item3d;

struct at_renderer3d {
    at_renderer         *renderer;
    at_pipeline         *pipelines[AT_PIPELINE3D_COUNT];
    at_texture          *white_texture; /* For materials without a texture */

    /* One uniform buffer per pass of a frame: each pass has its own camera */
    /* and lights                                                           */
    at_buffer           *frame_buffers[AT_RENDERER3D_MAX_PASSES];
    uint64_t             frame_number; /* Renderer frame the counts belong to */
    uint32_t             pass_count;   /* Passes drawn this frame             */
    at_renderer3d_stats  stats;        /* This frame so far                   */

    /* Collected since begin */
    at_draw_item3d      *items;
    uint32_t             item_count;
    uint32_t             item_capacity;
    at_camera3d          camera;

    at_directional_light directional_lights[AT_RENDERER3D_MAX_DIRECTIONAL_LIGHTS];
    at_point_light       point_lights[AT_RENDERER3D_MAX_POINT_LIGHTS];
    at_spot_light        spot_lights[AT_RENDERER3D_MAX_SPOT_LIGHTS];
    uint32_t             directional_count;
    uint32_t             point_count;
    uint32_t             spot_count;

    /* Settings */
    at_color             ambient_sky;
    at_color             ambient_ground;
    float                ambient_intensity;
    bool                 uses_lights; /* A light was added once: no default sun */

    bool                 active;
    bool                 inactive_warned;
    bool                 passes_warned;
    bool                 lights_warned;
};

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

static void set4(float out[4], float x, float y, float z, float w) {
    out[0] = x;
    out[1] = y;
    out[2] = z;
    out[3] = w;
}

static float srgb_to_linear(float c) {
    if (c <= 0.04045f) {
        return c / 12.92f;
    }
    return powf((c + 0.055f) / 1.055f, 2.4f);
}

/* A color for the shaders: linear, times the intensity; w as given */
static void light_color(float out[4], at_color color, float intensity, float w) {
    float k = at_maxf(intensity, 0.0f);
    set4(out, srgb_to_linear(color.r) * k, srgb_to_linear(color.g) * k,
         srgb_to_linear(color.b) * k, w);
}

/* Unit direction; straight down if it has no length */
static at_vec3 unit_direction(at_vec3 direction) {
    at_vec3 unit = at_vec3_normalize(direction);
    return at_vec3_length_sq(unit) > 0.0f ? unit : at_vec3_make(0.0f, -1.0f, 0.0f);
}

/* The sun of passes without lights, until the first light is added */
static at_directional_light default_sun(void) {
    return at_directional_light_make(at_vec3_make(-0.4f, -1.0f, -0.3f),
                                     at_color_rgb(0.98f, 0.95f, 0.90f), 1.0f);
}

static bool grow_items(at_renderer3d *renderer3d) {
    if (renderer3d->item_count < renderer3d->item_capacity) {
        return true;
    }

    uint32_t        capacity = renderer3d->item_capacity ? renderer3d->item_capacity * 2
                                                         : AT_RENDERER3D_INITIAL_ITEMS;
    at_draw_item3d *items    = (at_draw_item3d *)realloc(renderer3d->items,
                                                         capacity * sizeof(*items));
    if (!items) {
        fprintf(stderr, "[archtype] Out of memory collecting 3D meshes\n");
        return false;
    }

    renderer3d->items         = items;
    renderer3d->item_capacity = capacity;
    return true;
}

/* Opaque first, sorted to change pipelines and textures as little as   */
/* possible; then transparent, from the farthest to the nearest. Ties   */
/* keep the order they were drawn in.                                   */
static int compare_items(const void *a, const void *b) {
    const at_draw_item3d *x = (const at_draw_item3d *)a;
    const at_draw_item3d *y = (const at_draw_item3d *)b;

    bool x_transparent = (x->pipeline & AT_PIPELINE3D_TRANSPARENT) != 0;
    bool y_transparent = (y->pipeline & AT_PIPELINE3D_TRANSPARENT) != 0;
    if (x_transparent != y_transparent) {
        return x_transparent ? 1 : -1;
    }

    if (x_transparent) {
        if (x->distance != y->distance) {
            return x->distance > y->distance ? -1 : 1;
        }
    } else {
        if (x->pipeline != y->pipeline) {
            return x->pipeline < y->pipeline ? -1 : 1;
        }
        uintptr_t x_texture = (uintptr_t)x->material.base_texture;
        uintptr_t y_texture = (uintptr_t)y->material.base_texture;
        if (x_texture != y_texture) {
            return x_texture < y_texture ? -1 : 1;
        }
    }

    return x->sequence < y->sequence ? -1 : (x->sequence > y->sequence ? 1 : 0);
}

static void fill_object(const at_draw_item3d *item, at_object_constants3d *object) {
    const at_material *material = &item->material;

    object->model = item->model;
    set4(object->base_color, material->base_color.r, material->base_color.g,
         material->base_color.b, material->base_color.a);
    set4(object->emissive, material->emissive.r, material->emissive.g, material->emissive.b,
         at_maxf(material->emissive_strength, 0.0f));
    set4(object->params, at_maxf(material->specular, 0.0f), at_maxf(material->shininess, 1.0f),
         material->alpha_cutoff, material->unlit ? 1.0f : 0.0f);
    set4(object->uv_transform, material->uv_scale.x, material->uv_scale.y,
         material->uv_offset.x, material->uv_offset.y);
}

/* The lights of the pass, as the shaders read them */
static void fill_lights(const at_renderer3d *renderer3d, at_frame_uniforms3d *frame) {
    float intensity = at_maxf(renderer3d->ambient_intensity, 0.0f);
    light_color(frame->ambient_sky, renderer3d->ambient_sky, intensity, 1.0f);
    light_color(frame->ambient_ground, renderer3d->ambient_ground, intensity, 1.0f);

    /* Directional */
    at_directional_light sun               = default_sun();
    const at_directional_light *directional = renderer3d->directional_lights;
    uint32_t                    count       = renderer3d->directional_count;
    if (!renderer3d->uses_lights) {
        directional = &sun;
        count       = 1;
    }

    for (uint32_t i = 0; i < count; ++i) {
        at_vec3 direction = unit_direction(directional[i].direction);
        set4(frame->directional[i].direction, direction.x, direction.y, direction.z, 0.0f);
        light_color(frame->directional[i].color, directional[i].color,
                    directional[i].intensity, 0.0f);
    }
    frame->light_counts[0] = count;

    /* Point */
    for (uint32_t i = 0; i < renderer3d->point_count; ++i) {
        const at_point_light *light = &renderer3d->point_lights[i];
        set4(frame->point[i].position_range, light->position.x, light->position.y,
             light->position.z, at_maxf(light->range, 0.01f));
        light_color(frame->point[i].color, light->color, light->intensity, 0.0f);
    }
    frame->light_counts[1] = renderer3d->point_count;

    /* Spot: the cone edges as cosines; inner stays inside outer, so the */
    /* fade between them is never zero-width                             */
    for (uint32_t i = 0; i < renderer3d->spot_count; ++i) {
        const at_spot_light *light = &renderer3d->spot_lights[i];

        float   outer     = at_clampf(light->outer_angle, 0.01f, AT_PI - 0.01f);
        float   inner     = at_clampf(light->inner_angle, 0.0f, outer * 0.99f);
        at_vec3 direction = unit_direction(light->direction);

        set4(frame->spot[i].position_range, light->position.x, light->position.y,
             light->position.z, at_maxf(light->range, 0.01f));
        set4(frame->spot[i].direction_cos_outer, direction.x, direction.y, direction.z,
             cosf(outer));
        light_color(frame->spot[i].color_cos_inner, light->color, light->intensity, cosf(inner));
    }
    frame->light_counts[2] = renderer3d->spot_count;
    frame->light_counts[3] = 0;
}

/* Common checks for adding a light; false if it must be skipped */
static bool accept_light(at_renderer3d *renderer3d, uint32_t count, uint32_t max,
                         const char *kind) {
    if (!renderer3d->active) {
        if (!renderer3d->inactive_warned) {
            fprintf(stderr, "[archtype] 3D lights and meshes must be added between "
                            "at_renderer3d_begin and at_renderer3d_end\n");
            renderer3d->inactive_warned = true;
        }
        return false;
    }

    /* From now on, passes have only the lights they are given */
    renderer3d->uses_lights = true;

    if (count >= max) {
        if (!renderer3d->lights_warned) {
            fprintf(stderr, "[archtype] Too many %s lights in one 3D pass (max %u): "
                            "the rest are ignored\n", kind, (unsigned)max);
            renderer3d->lights_warned = true;
        }
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Creation and destruction                                                  */
/* ------------------------------------------------------------------------ */

static at_pipeline *create_pipeline(at_renderer *renderer, at_shader *vertex_shader,
                                    at_shader *fragment_shader, uint32_t variant) {
    bool double_sided = (variant & AT_PIPELINE3D_DOUBLE_SIDED) != 0;
    bool transparent  = (variant & AT_PIPELINE3D_TRANSPARENT) != 0;

    at_pipeline_desc desc;
    memset(&desc, 0, sizeof(desc));
    desc.vertex_shader   = vertex_shader;
    desc.fragment_shader = fragment_shader;
    desc.vertex_layout   = at_mesh_vertex_layout();

    /* Set 0: the pass's camera and lights (vertex and fragment) */
    desc.bindings[0].set     = 0;
    desc.bindings[0].binding = 0;
    desc.bindings[0].type    = AT_BINDING_UNIFORM_BUFFER;

    /* Set 1: the material's texture */
    desc.bindings[1].set     = 1;
    desc.bindings[1].binding = 0;
    desc.bindings[1].type    = AT_BINDING_TEXTURE;
    desc.bindings[1].stages  = AT_SHADER_STAGE_FRAGMENT;
    desc.binding_count       = 2;

    desc.push_constant_size = sizeof(at_object_constants3d);
    desc.cull_mode          = double_sided ? AT_CULL_NONE : AT_CULL_BACK;
    desc.blend_mode         = transparent ? AT_BLEND_ALPHA : AT_BLEND_NONE;
    desc.depth_test         = true;
    desc.depth_write        = !transparent; /* Transparent: don't hide what comes later */

    return at_pipeline_create(renderer, &desc);
}

static bool create_pipelines(at_renderer3d *renderer3d) {
    at_renderer *renderer = renderer3d->renderer;

    at_shader *vertex_shader   = at_shader_create(renderer, k_vertex_shader,
                                                  sizeof(k_vertex_shader));
    at_shader *fragment_shader = at_shader_create(renderer, k_fragment_shader,
                                                  sizeof(k_fragment_shader));

    bool ok = vertex_shader && fragment_shader;
    for (uint32_t variant = 0; ok && variant < AT_PIPELINE3D_COUNT; ++variant) {
        renderer3d->pipelines[variant] = create_pipeline(renderer, vertex_shader,
                                                         fragment_shader, variant);
        ok = renderer3d->pipelines[variant] != NULL;
    }

    /* The pipelines keep what they need */
    at_shader_destroy(renderer, vertex_shader);
    at_shader_destroy(renderer, fragment_shader);
    return ok;
}

static bool create_white_texture(at_renderer3d *renderer3d) {
    static const uint8_t white[4] = { 255, 255, 255, 255 };

    at_texture_options options;
    memset(&options, 0, sizeof(options));
    options.filter = AT_TEXTURE_FILTER_NEAREST;

    renderer3d->white_texture = at_texture_create(renderer3d->renderer, 1, 1, white, &options);
    return renderer3d->white_texture != NULL;
}

static bool create_frame_buffers(at_renderer3d *renderer3d) {
    at_buffer_desc desc;
    memset(&desc, 0, sizeof(desc));
    desc.type = AT_BUFFER_UNIFORM;
    desc.size = sizeof(at_frame_uniforms3d);

    for (uint32_t i = 0; i < AT_RENDERER3D_MAX_PASSES; ++i) {
        renderer3d->frame_buffers[i] = at_buffer_create(renderer3d->renderer, &desc);
        if (!renderer3d->frame_buffers[i]) {
            return false;
        }
    }
    return true;
}

at_renderer3d *at_renderer3d_create(at_renderer *renderer) {
    if (!renderer) {
        fprintf(stderr, "[archtype] at_renderer3d_create needs a renderer\n");
        return NULL;
    }

    at_renderer3d *renderer3d = (at_renderer3d *)calloc(1, sizeof(*renderer3d));
    if (!renderer3d) {
        fprintf(stderr, "[archtype] Failed to allocate the 3D renderer\n");
        return NULL;
    }

    renderer3d->renderer = renderer;
    renderer3d->camera   = at_camera3d_make();

    /* A soft bluish sky over a dark ground */
    renderer3d->ambient_sky       = at_color_rgb(0.44f, 0.47f, 0.54f);
    renderer3d->ambient_ground    = at_color_rgb(0.25f, 0.235f, 0.22f);
    renderer3d->ambient_intensity = 1.0f;

    if (!create_pipelines(renderer3d) || !create_white_texture(renderer3d) ||
        !create_frame_buffers(renderer3d) || !grow_items(renderer3d)) {
        at_renderer3d_destroy(renderer3d);
        return NULL;
    }

    fprintf(stderr, "[archtype] 3D renderer created\n");
    return renderer3d;
}

void at_renderer3d_destroy(at_renderer3d *renderer3d) {
    if (!renderer3d) {
        return;
    }

    at_renderer *renderer = renderer3d->renderer;

    for (uint32_t i = 0; i < AT_RENDERER3D_MAX_PASSES; ++i) {
        at_buffer_destroy(renderer, renderer3d->frame_buffers[i]);
    }
    at_texture_destroy(renderer, renderer3d->white_texture);
    for (uint32_t variant = 0; variant < AT_PIPELINE3D_COUNT; ++variant) {
        at_pipeline_destroy(renderer, renderer3d->pipelines[variant]);
    }
    free(renderer3d->items);
    free(renderer3d);
}

/* ------------------------------------------------------------------------ */
/* Begin and end                                                             */
/* ------------------------------------------------------------------------ */

void at_renderer3d_begin(at_renderer3d *renderer3d, const at_camera3d *camera) {
    if (!renderer3d) {
        return;
    }

    at_renderer *renderer = renderer3d->renderer;

    if (renderer3d->active) {
        fprintf(stderr, "[archtype] at_renderer3d_begin called twice without end\n");
        return;
    }
    if (!renderer->frame_started) {
        fprintf(stderr, "[archtype] at_renderer3d_begin must be called between "
                        "at_renderer_begin_frame and at_renderer_end_frame\n");
        return;
    }

    /* First begin of a new frame: every pass buffer is free again */
    if (renderer3d->frame_number != renderer->frame_number) {
        renderer3d->frame_number = renderer->frame_number;
        renderer3d->pass_count   = 0;
        memset(&renderer3d->stats, 0, sizeof(renderer3d->stats));
    }

    renderer3d->camera            = camera ? *camera : at_camera3d_make();
    renderer3d->item_count        = 0;
    renderer3d->directional_count = 0;
    renderer3d->point_count       = 0;
    renderer3d->spot_count        = 0;
    renderer3d->active            = true;
}

void at_renderer3d_end(at_renderer3d *renderer3d) {
    if (!renderer3d) {
        return;
    }
    if (!renderer3d->active) {
        fprintf(stderr, "[archtype] at_renderer3d_end called without begin\n");
        return;
    }
    renderer3d->active = false;

    if (renderer3d->item_count == 0) {
        return;
    }

    if (renderer3d->pass_count >= AT_RENDERER3D_MAX_PASSES) {
        if (!renderer3d->passes_warned) {
            fprintf(stderr, "[archtype] Too many 3D passes in one frame (max %d): "
                            "drawing skipped\n", AT_RENDERER3D_MAX_PASSES);
            renderer3d->passes_warned = true;
        }
        renderer3d->item_count = 0;
        return;
    }

    at_renderer *renderer     = renderer3d->renderer;
    at_buffer   *frame_buffer = renderer3d->frame_buffers[renderer3d->pass_count++];
    VkExtent2D   extent       = renderer->swapchain.extent;

    /* 1. What every mesh of this pass shares: the camera and the lights */
    at_frame_uniforms3d frame;
    memset(&frame, 0, sizeof(frame));
    frame.view_projection = at_camera3d_view_projection(&renderer3d->camera,
                                                        (float)extent.width,
                                                        (float)extent.height);
    set4(frame.camera_position, renderer3d->camera.position.x, renderer3d->camera.position.y,
         renderer3d->camera.position.z, 1.0f);
    fill_lights(renderer3d, &frame);

    renderer3d->stats.lights += frame.light_counts[0] + frame.light_counts[1]
                              + frame.light_counts[2];

    at_buffer_update(renderer, frame_buffer, &frame, sizeof(frame), 0);

    /* 2. Opaque first, transparent last and back to front */
    qsort(renderer3d->items, renderer3d->item_count, sizeof(at_draw_item3d), compare_items);

    /* 3. Draw. Set 0 stays the same for the whole pass; set 1 is only */
    /*    rebuilt when the texture changes from one mesh to the next.  */
    at_renderer_bind_uniform_buffer_in_set(renderer, 0, 0, frame_buffer);

    uint32_t bound_pipeline = AT_PIPELINE3D_COUNT; /* None yet */
    for (uint32_t i = 0; i < renderer3d->item_count; ++i) {
        const at_draw_item3d *item = &renderer3d->items[i];

        if (item->pipeline != bound_pipeline) {
            at_renderer_bind_pipeline(renderer, renderer3d->pipelines[item->pipeline]);
            bound_pipeline = item->pipeline;
        }

        at_texture *texture = item->material.base_texture ? item->material.base_texture
                                                          : renderer3d->white_texture;
        at_renderer_bind_texture_in_set(renderer, 1, 0, texture);

        at_object_constants3d object;
        fill_object(item, &object);
        at_renderer_push_constants(renderer, &object, sizeof(object));
        at_mesh_draw(renderer, item->mesh);

        renderer3d->stats.draw_calls++;
        renderer3d->stats.triangles += at_mesh_get_index_count(item->mesh) / 3;
        if (item->pipeline & AT_PIPELINE3D_TRANSPARENT) {
            renderer3d->stats.transparent++;
        }
    }

    renderer3d->item_count = 0;
}

at_renderer3d_stats at_renderer3d_get_stats(const at_renderer3d *renderer3d) {
    at_renderer3d_stats stats;
    memset(&stats, 0, sizeof(stats));
    if (renderer3d) {
        stats = renderer3d->stats;
    }
    return stats;
}

/* ------------------------------------------------------------------------ */
/* Meshes                                                                    */
/* ------------------------------------------------------------------------ */

void at_renderer3d_draw_mesh_ex(at_renderer3d *renderer3d, const at_mesh *mesh, at_mat4 model,
                                const at_material *material) {
    if (!renderer3d || !mesh) {
        return;
    }
    if (!renderer3d->active) {
        if (!renderer3d->inactive_warned) {
            fprintf(stderr, "[archtype] 3D lights and meshes must be added between "
                            "at_renderer3d_begin and at_renderer3d_end\n");
            renderer3d->inactive_warned = true;
        }
        return;
    }
    if (!grow_items(renderer3d)) {
        return;
    }

    at_draw_item3d *item = &renderer3d->items[renderer3d->item_count];
    item->mesh     = mesh;
    item->model    = model;
    item->material = material ? *material : at_material_make();
    item->sequence = renderer3d->item_count;

    item->pipeline = 0;
    if (item->material.double_sided) {
        item->pipeline |= AT_PIPELINE3D_DOUBLE_SIDED;
    }
    if (item->material.transparent) {
        item->pipeline |= AT_PIPELINE3D_TRANSPARENT;
    }

    /* The object's origin (the translation of its model matrix) */
    float dx = model.m[3][0] - renderer3d->camera.position.x;
    float dy = model.m[3][1] - renderer3d->camera.position.y;
    float dz = model.m[3][2] - renderer3d->camera.position.z;
    item->distance = dx * dx + dy * dy + dz * dz;

    renderer3d->item_count++;
}

void at_renderer3d_draw_mesh(at_renderer3d *renderer3d, const at_mesh *mesh, at_mat4 model,
                             at_color color) {
    at_material material = at_material_make();
    material.base_color  = color;
    at_renderer3d_draw_mesh_ex(renderer3d, mesh, model, &material);
}

/* ------------------------------------------------------------------------ */
/* Lights                                                                    */
/* ------------------------------------------------------------------------ */

void at_renderer3d_add_directional_light(at_renderer3d *renderer3d,
                                         const at_directional_light *light) {
    if (!renderer3d || !light ||
        !accept_light(renderer3d, renderer3d->directional_count,
                      AT_RENDERER3D_MAX_DIRECTIONAL_LIGHTS, "directional")) {
        return;
    }
    renderer3d->directional_lights[renderer3d->directional_count++] = *light;
}

void at_renderer3d_add_point_light(at_renderer3d *renderer3d, const at_point_light *light) {
    if (!renderer3d || !light ||
        !accept_light(renderer3d, renderer3d->point_count, AT_RENDERER3D_MAX_POINT_LIGHTS,
                      "point")) {
        return;
    }
    renderer3d->point_lights[renderer3d->point_count++] = *light;
}

void at_renderer3d_add_spot_light(at_renderer3d *renderer3d, const at_spot_light *light) {
    if (!renderer3d || !light ||
        !accept_light(renderer3d, renderer3d->spot_count, AT_RENDERER3D_MAX_SPOT_LIGHTS,
                      "spot")) {
        return;
    }
    renderer3d->spot_lights[renderer3d->spot_count++] = *light;
}

void at_renderer3d_set_ambient(at_renderer3d *renderer3d, at_color sky, at_color ground,
                               float intensity) {
    if (!renderer3d) {
        return;
    }
    renderer3d->ambient_sky       = sky;
    renderer3d->ambient_ground    = ground;
    renderer3d->ambient_intensity = intensity;
}