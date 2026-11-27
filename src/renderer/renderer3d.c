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

/* Set 0, binding 0: what every mesh of a pass shares. Must match "uniform */
/* Frame" in the 3D shaders (std140: only mat4 and vec4). Colors here are  */
/* linear, ready for the lighting math.                                    */
typedef struct at_frame_uniforms3d {
    at_mat4 view_projection;
    float   camera_position[4]; /* xyz                              */
    float   light_direction[4]; /* xyz: where the sun light travels */
    float   light_color[4];     /* rgb                              */
    float   ambient_sky[4];     /* rgb: light on faces pointing up  */
    float   ambient_ground[4];  /* rgb: light on faces pointing down */
} at_frame_uniforms3d;

/* Per mesh, in push constants: 128 bytes, the most every GPU allows.  */
/* Must match "push_constant Object" in the 3D shaders.                */
typedef struct at_object_constants3d {
    at_mat4 model;
    float   normal_matrix[3][4]; /* Columns of the normal matrix, as vec4 */
    float   color[4];            /* sRGB                                 */
} at_object_constants3d;

/* One mesh waiting for end */
typedef struct at_draw_item3d {
    const at_mesh *mesh;
    at_mat4        model;
    at_color       color;
} at_draw_item3d;

struct at_renderer3d {
    at_renderer        *renderer;
    at_pipeline        *pipeline;

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

/* The matrix that turns normals with the object: the inverse transpose */
/* of the model's rotation and scale. With a different scale on each    */
/* axis, the model matrix itself would tilt normals off the surface.    */
static void make_normal_matrix(at_mat4 model, float out[3][4]) {
    at_mat4 inverse_transpose = at_mat4_transpose(at_mat4_inverse(model));

    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
            out[column][row] = inverse_transpose.m[column][row];
        }
        out[column][3] = 0.0f;
    }
}

static void copy3(float out[4], float x, float y, float z, float w) {
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

static bool create_pipeline(at_renderer3d *renderer3d) {
    at_renderer *renderer = renderer3d->renderer;

    at_shader *vertex_shader   = at_shader_create(renderer, k_vertex_shader,
                                                  sizeof(k_vertex_shader));
    at_shader *fragment_shader = at_shader_create(renderer, k_fragment_shader,
                                                  sizeof(k_fragment_shader));

    if (vertex_shader && fragment_shader) {
        at_pipeline_desc desc;
        memset(&desc, 0, sizeof(desc));
        desc.vertex_shader   = vertex_shader;
        desc.fragment_shader = fragment_shader;
        desc.vertex_layout   = at_mesh_vertex_layout();

        /* Set 0: the frame's camera and lighting, vertex and fragment */
        desc.binding_count       = 1;
        desc.bindings[0].set     = 0;
        desc.bindings[0].binding = 0;
        desc.bindings[0].type    = AT_BINDING_UNIFORM_BUFFER;

        desc.push_constant_size = sizeof(at_object_constants3d);
        desc.cull_mode          = AT_CULL_BACK;
        desc.depth_test         = true;
        desc.depth_write        = true;

        renderer3d->pipeline = at_pipeline_create(renderer, &desc);
    }

    /* The pipeline keeps what it needs */
    at_shader_destroy(renderer, vertex_shader);
    at_shader_destroy(renderer, fragment_shader);
    return renderer3d->pipeline != NULL;
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

/* ------------------------------------------------------------------------ */
/* Creation and destruction                                                  */
/* ------------------------------------------------------------------------ */

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

    if (!create_pipeline(renderer3d) || !create_frame_buffers(renderer3d) ||
        !grow_items(renderer3d)) {
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
    at_pipeline_destroy(renderer, renderer3d->pipeline);
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
    copy3(frame.camera_position, renderer3d->camera.position.x,
          renderer3d->camera.position.y, renderer3d->camera.position.z, 1.0f);
    copy3(frame.light_direction, renderer3d->light_direction.x,
          renderer3d->light_direction.y, renderer3d->light_direction.z, 0.0f);
    copy3(frame.light_color, renderer3d->light_color[0], renderer3d->light_color[1],
          renderer3d->light_color[2], 1.0f);
    copy3(frame.ambient_sky, renderer3d->ambient_sky[0], renderer3d->ambient_sky[1],
          renderer3d->ambient_sky[2], 1.0f);
    copy3(frame.ambient_ground, renderer3d->ambient_ground[0], renderer3d->ambient_ground[1],
          renderer3d->ambient_ground[2], 1.0f);

    at_buffer_update(renderer, frame_buffer, &frame, sizeof(frame), 0);

    at_renderer_bind_pipeline(renderer, renderer3d->pipeline);
    at_renderer_bind_uniform_buffer_in_set(renderer, 0, 0, frame_buffer);

    /* 2. Each mesh: its matrices and color in push constants, then a draw */
    for (uint32_t i = 0; i < renderer3d->item_count; ++i) {
        const at_draw_item3d *item = &renderer3d->items[i];

        at_object_constants3d object;
        object.model = item->model;
        make_normal_matrix(item->model, object.normal_matrix);
        copy3(object.color, item->color.r, item->color.g, item->color.b, item->color.a);

        at_renderer_push_constants(renderer, &object, sizeof(object));
        at_mesh_draw(renderer, item->mesh);

        renderer3d->stats.draw_calls++;
        renderer3d->stats.triangles += at_mesh_get_index_count(item->mesh) / 3;
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

void at_renderer3d_draw_mesh(at_renderer3d *renderer3d, const at_mesh *mesh, at_mat4 model,
                             at_color color) {
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

    at_draw_item3d *item = &renderer3d->items[renderer3d->item_count++];
    item->mesh  = mesh;
    item->model = model;
    item->color = color;
}