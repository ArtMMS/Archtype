#include "archtype/renderer/renderer3d.h"
#include "renderer/renderer_internal.h"

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

/* Set 0, binding 0: what every mesh of a pass shares. Must match "uniform */
/* Frame" in the 3D shaders (std140: only mat4 and vec4). Colors here are  */
/* linear, ready for the lighting math.                                    */
typedef struct at_frame_uniforms3d {
    at_mat4 view_projection;
    float   camera_position[4]; /* xyz                               */
    float   light_direction[4]; /* xyz: where the sun light travels  */
    float   light_color[4];     /* rgb                               */
    float   ambient_sky[4];     /* rgb: light on faces pointing up   */
    float   ambient_ground[4];  /* rgb: light on faces pointing down */
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
    at_renderer        *renderer;
    at_pipeline        *pipelines[AT_PIPELINE3D_COUNT];
    at_texture         *white_texture; /* For materials without a texture */

    /* One uniform buffer per pass of a frame: each pass has its own camera */
    at_buffer          *frame_buffers[AT_RENDERER3D_MAX_PASSES];
    uint64_t            frame_number; /* Renderer frame the counts belong to */
    uint32_t            pass_count;   /* Passes drawn this frame             */
    at_renderer3d_stats stats;        /* This frame so far                   */

    /* Collected since begin */
    at_draw_item3d     *items;
    uint32_t            item_count;
    uint32_t            item_capacity;
    at_camera3d         camera;

    /* Fixed lighting, until the lights of the roadmap */
    at_vec3             light_direction;
    float               light_color[3];
    float               ambient_sky[3];
    float               ambient_ground[3];

    bool                active;
    bool                inactive_warned;
    bool                passes_warned;
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

    /* Set 0: the pass's camera and lighting (vertex and fragment) */
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

    /* A warm sun from above and to the side, a bluish sky, a dark ground */
    renderer3d->light_direction   = at_vec3_normalize(at_vec3_make(-0.4f, -1.0f, -0.3f));
    renderer3d->light_color[0]    = 0.95f;
    renderer3d->light_color[1]    = 0.90f;
    renderer3d->light_color[2]    = 0.80f;
    renderer3d->ambient_sky[0]    = 0.16f;
    renderer3d->ambient_sky[1]    = 0.19f;
    renderer3d->ambient_sky[2]    = 0.25f;
    renderer3d->ambient_ground[0] = 0.05f;
    renderer3d->ambient_ground[1] = 0.045f;
    renderer3d->ambient_ground[2] = 0.04f;

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

    renderer3d->camera     = camera ? *camera : at_camera3d_make();
    renderer3d->item_count = 0;
    renderer3d->active     = true;
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

    /* 1. What every mesh of this pass shares */
    at_frame_uniforms3d frame;
    frame.view_projection = at_camera3d_view_projection(&renderer3d->camera,
                                                        (float)extent.width,
                                                        (float)extent.height);
    set4(frame.camera_position, renderer3d->camera.position.x, renderer3d->camera.position.y,
         renderer3d->camera.position.z, 1.0f);
    set4(frame.light_direction, renderer3d->light_direction.x, renderer3d->light_direction.y,
         renderer3d->light_direction.z, 0.0f);
    set4(frame.light_color, renderer3d->light_color[0], renderer3d->light_color[1],
         renderer3d->light_color[2], 1.0f);
    set4(frame.ambient_sky, renderer3d->ambient_sky[0], renderer3d->ambient_sky[1],
         renderer3d->ambient_sky[2], 1.0f);
    set4(frame.ambient_ground, renderer3d->ambient_ground[0], renderer3d->ambient_ground[1],
         renderer3d->ambient_ground[2], 1.0f);

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
/* Drawing                                                                   */
/* ------------------------------------------------------------------------ */

void at_renderer3d_draw_mesh_ex(at_renderer3d *renderer3d, const at_mesh *mesh, at_mat4 model,
                                const at_material *material) {
    if (!renderer3d || !mesh) {
        return;
    }
    if (!renderer3d->active) {
        if (!renderer3d->inactive_warned) {
            fprintf(stderr, "[archtype] 3D meshes must be drawn between at_renderer3d_begin "
                            "and at_renderer3d_end\n");
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