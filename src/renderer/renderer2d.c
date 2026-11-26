#include "archtype/renderer/renderer2d.h"
#include "renderer/renderer_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* SPIR-V of the 2D shaders, compiled from src/renderer/shaders/ by CMake */
/* (archtype_embed_shaders), as C arrays.                                 */
static const uint32_t k_vertex_shader[] =
#include "embedded/renderer2d.vert.inc"
;

static const uint32_t k_fragment_shader[] =
#include "embedded/renderer2d.frag.inc"
;

/* Texture coordinate of the shapes: the center of the 1x1 white texture */
#define WHITE_UV 0.5f

/* Circles: segments by radius on screen, in pixels */
#define AT_CIRCLE_MIN_SEGMENTS 12
#define AT_CIRCLE_MAX_SEGMENTS 128

/* One pipeline per blend mode: AT_BLEND_NONE, AT_BLEND_ALPHA, AT_BLEND_ADDITIVE */
#define AT_BLEND_MODE_COUNT 3

/* Must match the inputs of renderer2d.vert */
typedef struct at_vertex2d {
    float   position[2];
    float   uv[2];
    uint8_t color[4]; /* sRGB, 0..255 */
} at_vertex2d;

/* Index buffer capacity: indices per vertex of max_vertices. Quads use */
/* 1.5, triangles 1, circles almost 3, so 2 covers almost every scene.  */
#define AT_INDICES_PER_VERTEX 2

/* A run of geometry drawn with the same state, waiting for end to be */
/* sorted. Consecutive draws with the same state share one item.      */
typedef struct at_draw_item2d {
    int32_t     layer;
    uint32_t    sequence;     /* Order it was drawn in: keeps ties in order */
    at_texture *texture;
    uint32_t    blend_mode;
    uint32_t    first_vertex; /* In staged_vertices */
    uint32_t    vertex_count;
    uint32_t    first_index;  /* In staged_indices  */
    uint32_t    index_count;
} at_draw_item2d;

/* Room taken for one shape or sprite: write the vertices, then the */
/* indices as base + 0, base + 1...                                  */
typedef struct at_geometry2d {
    at_vertex2d *vertices;
    uint32_t    *indices;
    uint32_t     base; /* Number of the first vertex, for the indices */
} at_geometry2d;

struct at_renderer2d {
    at_renderer        *renderer;
    at_pipeline        *pipelines[AT_BLEND_MODE_COUNT];
    at_texture         *white_texture; /* Lets shapes use the same shader as sprites */
    uint32_t            max_vertices;
    uint32_t            max_indices;

    /* GPU buffers, one pair per frame in flight: the CPU fills one while */
    /* the GPU reads the other. Several begin/end pairs in a frame append */
    /* to the same buffers.                                               */
    at_vk_buffer        vertex_buffers[AT_VK_FRAMES_IN_FLIGHT];
    at_vk_buffer        index_buffers[AT_VK_FRAMES_IN_FLIGHT];
    uint64_t            frame_number;       /* Renderer frame the counts belong to */
    uint32_t            frame_vertex_count; /* Vertices sent this frame            */
    uint32_t            frame_index_count;  /* Indices sent this frame             */
    at_renderer2d_stats stats;              /* This frame so far                   */

    /* Drawn since begin, waiting for end: CPU memory, in draw order */
    at_vertex2d        *staged_vertices;
    uint32_t           *staged_indices; /* Point into staged_vertices */
    uint32_t            staged_vertex_count;
    uint32_t            staged_index_count;
    at_draw_item2d     *items;
    uint32_t            item_count;
    uint32_t            max_items;
    bool                needs_sort; /* An item went below the layer of the one before */

    /* Draw state, reset by begin */
    int32_t             layer;
    at_blend_mode       blend_mode;

    /* Between begin and end */
    bool                active;
    bool                inactive_warned;
    bool                full_warned; /* "Buffer full" is only reported once */
    at_mat4             view_projection;
    float               zoom;
};

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

static uint8_t to_byte(float value) {
    return (uint8_t)(at_clampf(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

static void set_vertex(at_vertex2d *vertex, float x, float y, float u, float v,
                       const uint8_t color[4]) {
    vertex->position[0] = x;
    vertex->position[1] = y;
    vertex->uv[0]       = u;
    vertex->uv[1]       = v;
    vertex->color[0]    = color[0];
    vertex->color[1]    = color[1];
    vertex->color[2]    = color[2];
    vertex->color[3]    = color[3];
}

static void pack_color(at_color color, uint8_t out[4]) {
    out[0] = to_byte(color.r);
    out[1] = to_byte(color.g);
    out[2] = to_byte(color.b);
    out[3] = to_byte(color.a);
}

/* Takes room for one shape or sprite of vertex_count vertices and        */
/* index_count indices, drawn with texture and the current layer and      */
/* blend mode. Returns false (and the drawing is skipped) outside         */
/* begin/end or when the frame's room is used up.                         */
static bool reserve(at_renderer2d *renderer2d, uint32_t vertex_count, uint32_t index_count,
                    at_texture *texture, at_geometry2d *out) {
    if (!renderer2d->active) {
        if (!renderer2d->inactive_warned) {
            fprintf(stderr, "[archtype] 2D shapes and sprites must be drawn between "
                            "at_renderer2d_begin and at_renderer2d_end\n");
            renderer2d->inactive_warned = true;
        }
        return false;
    }

    /* Room left this frame, counting what earlier begin/end pairs sent */
    uint32_t vertex_room = renderer2d->max_vertices - renderer2d->frame_vertex_count
                         - renderer2d->staged_vertex_count;
    uint32_t index_room  = renderer2d->max_indices - renderer2d->frame_index_count
                         - renderer2d->staged_index_count;

    at_draw_item2d *last  = renderer2d->item_count > 0
                          ? &renderer2d->items[renderer2d->item_count - 1] : NULL;
    bool            joins = last && last->layer == renderer2d->layer
                         && last->texture == texture
                         && last->blend_mode == (uint32_t)renderer2d->blend_mode;

    if (vertex_count > vertex_room || index_count > index_room ||
        (!joins && renderer2d->item_count == renderer2d->max_items)) {
        renderer2d->stats.skipped++;
        if (!renderer2d->full_warned) {
            fprintf(stderr, "[archtype] 2D renderer is full (%u vertices per frame): "
                            "drawing skipped. Raise at_renderer2d_desc.max_vertices\n",
                    renderer2d->max_vertices);
            renderer2d->full_warned = true;
        }
        return false;
    }

    if (joins) {
        /* Same state as the draw before: just make its run longer */
        last->vertex_count += vertex_count;
        last->index_count  += index_count;
    } else {
        at_draw_item2d *item = &renderer2d->items[renderer2d->item_count];
        item->layer        = renderer2d->layer;
        item->sequence     = renderer2d->item_count;
        item->texture      = texture;
        item->blend_mode   = (uint32_t)renderer2d->blend_mode;
        item->first_vertex = renderer2d->staged_vertex_count;
        item->vertex_count = vertex_count;
        item->first_index  = renderer2d->staged_index_count;
        item->index_count  = index_count;

        if (last && item->layer < last->layer) {
            renderer2d->needs_sort = true;
        }
        renderer2d->item_count++;
    }

    out->vertices = &renderer2d->staged_vertices[renderer2d->staged_vertex_count];
    out->indices  = &renderer2d->staged_indices[renderer2d->staged_index_count];
    out->base     = renderer2d->staged_vertex_count;

    renderer2d->staged_vertex_count += vertex_count;
    renderer2d->staged_index_count  += index_count;
    renderer2d->stats.primitives++;
    return true;
}

/* Lower layers first; in the same layer, the order they were drawn in */
static int compare_items(const void *a, const void *b) {
    const at_draw_item2d *x = (const at_draw_item2d *)a;
    const at_draw_item2d *y = (const at_draw_item2d *)b;

    if (x->layer != y->layer) {
        return x->layer < y->layer ? -1 : 1;
    }
    return x->sequence < y->sequence ? -1 : (x->sequence > y->sequence ? 1 : 0);
}

/* One draw call: index_count indices from first_index in this frame's */
/* index buffer (the indices point into the whole vertex buffer).      */
static void draw_batch(at_renderer2d *renderer2d, at_vk_buffer *index_buffer,
                       at_texture *texture, uint32_t blend_mode,
                       uint32_t first_index, uint32_t index_count) {
    at_renderer    *renderer       = renderer2d->renderer;
    VkCommandBuffer command_buffer = at_renderer_command_buffer(renderer);
    VkDeviceSize    offset         = (VkDeviceSize)first_index * sizeof(uint32_t);

    at_renderer_bind_pipeline(renderer, renderer2d->pipelines[blend_mode]);
    at_renderer_bind_texture(renderer, 0, texture);
    at_renderer_push_constants(renderer, &renderer2d->view_projection, sizeof(at_mat4));

    /* The batch starts in the middle of the index buffer: bind it at an  */
    /* offset, and tell the renderer an index buffer is bound.            */
    vkCmdBindIndexBuffer(command_buffer, index_buffer->handle, offset, VK_INDEX_TYPE_UINT32);
    renderer->index_buffer_bound = true;
    at_renderer_draw_indexed(renderer, index_count);

    renderer2d->stats.draw_calls++;
}

/* Sorts the staged items, copies their vertices and indices to this    */
/* frame's GPU buffers in that order and draws them, one draw call per  */
/* run of the same texture and blend mode.                              */
static void submit(at_renderer2d *renderer2d) {
    if (renderer2d->item_count == 0) {
        return;
    }

    if (renderer2d->needs_sort) {
        qsort(renderer2d->items, renderer2d->item_count, sizeof(at_draw_item2d),
              compare_items);
    }

    at_renderer  *renderer      = renderer2d->renderer;
    uint32_t      frame         = at_renderer_frame_index(renderer);
    at_vk_buffer *vertex_buffer = &renderer2d->vertex_buffers[frame];
    at_vk_buffer *index_buffer  = &renderer2d->index_buffers[frame];
    at_vertex2d  *gpu_vertices  = (at_vertex2d *)vertex_buffer->mapped;
    uint32_t     *gpu_indices   = (uint32_t *)index_buffer->mapped;

    uint32_t first_vertex = renderer2d->frame_vertex_count;
    uint32_t first_index  = renderer2d->frame_index_count;
    uint32_t vertex_write = first_vertex;
    uint32_t index_write  = first_index;

    /* 1. Copy every item, in sorted order, after what earlier begin/end */
    /*    pairs of this frame already sent. Its indices are moved to     */
    /*    where its vertices land in the GPU buffer.                     */
    for (uint32_t i = 0; i < renderer2d->item_count; ++i) {
        const at_draw_item2d *item = &renderer2d->items[i];

        memcpy(&gpu_vertices[vertex_write], &renderer2d->staged_vertices[item->first_vertex],
               (size_t)item->vertex_count * sizeof(at_vertex2d));

        const uint32_t *staged = &renderer2d->staged_indices[item->first_index];
        uint32_t       *dst    = &gpu_indices[index_write];
        for (uint32_t j = 0; j < item->index_count; ++j) {
            dst[j] = staged[j] - item->first_vertex + vertex_write;
        }

        vertex_write += item->vertex_count;
        index_write  += item->index_count;
    }

    /* Make the CPU writes visible to the GPU (no-op on coherent memory) */
    at_vk_buffer_flush(vertex_buffer, &renderer->allocator,
                       (VkDeviceSize)first_vertex * sizeof(at_vertex2d),
                       (VkDeviceSize)(vertex_write - first_vertex) * sizeof(at_vertex2d));
    at_vk_buffer_flush(index_buffer, &renderer->allocator,
                       (VkDeviceSize)first_index * sizeof(uint32_t),
                       (VkDeviceSize)(index_write - first_index) * sizeof(uint32_t));

    /* 2. Draw: the whole vertex buffer stays bound; neighbours with the  */
    /*    same texture and blend mode are one batch.                      */
    VkDeviceSize no_offset = 0;
    vkCmdBindVertexBuffers(at_renderer_command_buffer(renderer), 0, 1, &vertex_buffer->handle,
                           &no_offset);

    uint32_t    batch_start   = first_index;
    uint32_t    batch_count   = 0;
    at_texture *batch_texture = renderer2d->items[0].texture;
    uint32_t    batch_blend   = renderer2d->items[0].blend_mode;

    for (uint32_t i = 0; i < renderer2d->item_count; ++i) {
        const at_draw_item2d *item = &renderer2d->items[i];

        if (item->texture != batch_texture || item->blend_mode != batch_blend) {
            draw_batch(renderer2d, index_buffer, batch_texture, batch_blend,
                       batch_start, batch_count);
            batch_start  += batch_count;
            batch_count   = 0;
            batch_texture = item->texture;
            batch_blend   = item->blend_mode;
        }
        batch_count += item->index_count;
    }
    draw_batch(renderer2d, index_buffer, batch_texture, batch_blend, batch_start, batch_count);

    renderer2d->stats.vertices    += vertex_write - first_vertex;
    renderer2d->stats.indices     += index_write - first_index;
    renderer2d->frame_vertex_count = vertex_write;
    renderer2d->frame_index_count  = index_write;
}

/* ------------------------------------------------------------------------ */
/* Creation and destruction                                                  */
/* ------------------------------------------------------------------------ */

static at_pipeline *create_pipeline(at_renderer2d *renderer2d, at_shader *vertex_shader,
                                    at_shader *fragment_shader, at_blend_mode blend_mode) {
    at_pipeline_desc desc;
    memset(&desc, 0, sizeof(desc));
    desc.vertex_shader   = vertex_shader;
    desc.fragment_shader = fragment_shader;

    desc.vertex_layout.stride          = sizeof(at_vertex2d);
    desc.vertex_layout.attribute_count = 3;
    desc.vertex_layout.attributes[0].location = 0;
    desc.vertex_layout.attributes[0].format   = AT_VERTEX_FORMAT_FLOAT2;
    desc.vertex_layout.attributes[0].offset   = offsetof(at_vertex2d, position);
    desc.vertex_layout.attributes[1].location = 1;
    desc.vertex_layout.attributes[1].format   = AT_VERTEX_FORMAT_FLOAT2;
    desc.vertex_layout.attributes[1].offset   = offsetof(at_vertex2d, uv);
    desc.vertex_layout.attributes[2].location = 2;
    desc.vertex_layout.attributes[2].format   = AT_VERTEX_FORMAT_UBYTE4_NORM;
    desc.vertex_layout.attributes[2].offset   = offsetof(at_vertex2d, color);

    desc.binding_count       = 1;
    desc.bindings[0].binding = 0;
    desc.bindings[0].type    = AT_BINDING_TEXTURE;
    desc.bindings[0].stages  = AT_SHADER_STAGE_FRAGMENT;

    desc.push_constant_size = sizeof(at_mat4);
    desc.cull_mode          = AT_CULL_NONE; /* Shapes can be given in any order */
    desc.blend_mode         = blend_mode;

    return at_pipeline_create(renderer2d->renderer, &desc);
}

static bool create_pipelines(at_renderer2d *renderer2d) {
    at_renderer *renderer = renderer2d->renderer;

    at_shader *vertex_shader   = at_shader_create(renderer, k_vertex_shader,
                                                  sizeof(k_vertex_shader));
    at_shader *fragment_shader = at_shader_create(renderer, k_fragment_shader,
                                                  sizeof(k_fragment_shader));

    bool ok = vertex_shader && fragment_shader;
    for (uint32_t mode = 0; ok && mode < AT_BLEND_MODE_COUNT; ++mode) {
        renderer2d->pipelines[mode] = create_pipeline(renderer2d, vertex_shader,
                                                      fragment_shader, (at_blend_mode)mode);
        ok = renderer2d->pipelines[mode] != NULL;
    }

    /* The pipelines keep what they need */
    at_shader_destroy(renderer, vertex_shader);
    at_shader_destroy(renderer, fragment_shader);
    return ok;
}

static bool create_white_texture(at_renderer2d *renderer2d) {
    static const uint8_t white[4] = { 255, 255, 255, 255 };

    at_texture_options options;
    memset(&options, 0, sizeof(options));
    options.filter = AT_TEXTURE_FILTER_NEAREST;
    options.wrap   = AT_TEXTURE_WRAP_CLAMP;

    renderer2d->white_texture = at_texture_create(renderer2d->renderer, 1, 1, white, &options);
    return renderer2d->white_texture != NULL;
}

static bool create_buffers(at_renderer2d *renderer2d) {
    at_vk_allocator *allocator    = &renderer2d->renderer->allocator;
    VkDeviceSize     vertex_bytes = (VkDeviceSize)renderer2d->max_vertices * sizeof(at_vertex2d);
    VkDeviceSize     index_bytes  = (VkDeviceSize)renderer2d->max_indices * sizeof(uint32_t);

    for (uint32_t i = 0; i < AT_VK_FRAMES_IN_FLIGHT; ++i) {
        if (!at_vk_buffer_create_host(&renderer2d->vertex_buffers[i], allocator, vertex_bytes,
                                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
            !at_vk_buffer_create_host(&renderer2d->index_buffers[i], allocator, index_bytes,
                                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) {
            return false;
        }
    }

    /* Every item has at least 3 vertices (one triangle) */
    renderer2d->max_items       = renderer2d->max_vertices / 3 + 1;
    renderer2d->staged_vertices = (at_vertex2d *)malloc((size_t)renderer2d->max_vertices
                                                        * sizeof(at_vertex2d));
    renderer2d->staged_indices  = (uint32_t *)malloc((size_t)renderer2d->max_indices
                                                     * sizeof(uint32_t));
    renderer2d->items           = (at_draw_item2d *)malloc((size_t)renderer2d->max_items
                                                           * sizeof(at_draw_item2d));
    if (!renderer2d->staged_vertices || !renderer2d->staged_indices || !renderer2d->items) {
        fprintf(stderr, "[archtype] Out of memory creating the 2D renderer\n");
        return false;
    }
    return true;
}

at_renderer2d *at_renderer2d_create(at_renderer *renderer, const at_renderer2d_desc *desc) {
    if (!renderer) {
        fprintf(stderr, "[archtype] at_renderer2d_create needs a renderer\n");
        return NULL;
    }

    at_renderer2d *renderer2d = (at_renderer2d *)calloc(1, sizeof(*renderer2d));
    if (!renderer2d) {
        fprintf(stderr, "[archtype] Failed to allocate the 2D renderer\n");
        return NULL;
    }

    renderer2d->renderer     = renderer;
    renderer2d->max_vertices = (desc && desc->max_vertices > 0)
                             ? desc->max_vertices
                             : AT_RENDERER2D_DEFAULT_MAX_VERTICES;
    renderer2d->max_indices  = renderer2d->max_vertices * AT_INDICES_PER_VERTEX;
    renderer2d->zoom         = 1.0f;
    renderer2d->blend_mode   = AT_BLEND_ALPHA;

    if (!create_pipelines(renderer2d) || !create_white_texture(renderer2d) ||
        !create_buffers(renderer2d)) {
        at_renderer2d_destroy(renderer2d);
        return NULL;
    }

    fprintf(stderr, "[archtype] 2D renderer created (%u vertices per frame)\n",
            renderer2d->max_vertices);
    return renderer2d;
}

void at_renderer2d_destroy(at_renderer2d *renderer2d) {
    if (!renderer2d) {
        return;
    }

    at_renderer *renderer = renderer2d->renderer;

    /* The GPU may still be reading the buffers */
    vkDeviceWaitIdle(renderer->context.device);

    for (uint32_t i = 0; i < AT_VK_FRAMES_IN_FLIGHT; ++i) {
        at_vk_buffer_destroy(&renderer2d->vertex_buffers[i], &renderer->allocator);
        at_vk_buffer_destroy(&renderer2d->index_buffers[i], &renderer->allocator);
    }
    at_texture_destroy(renderer, renderer2d->white_texture);
    for (uint32_t mode = 0; mode < AT_BLEND_MODE_COUNT; ++mode) {
        at_pipeline_destroy(renderer, renderer2d->pipelines[mode]);
    }
    free(renderer2d->items);
    free(renderer2d->staged_indices);
    free(renderer2d->staged_vertices);
    free(renderer2d);
}

/* ------------------------------------------------------------------------ */
/* Begin and end                                                             */
/* ------------------------------------------------------------------------ */

void at_renderer2d_begin(at_renderer2d *renderer2d, const at_camera2d *camera) {
    if (!renderer2d) {
        return;
    }

    at_renderer *renderer = renderer2d->renderer;

    if (renderer2d->active) {
        fprintf(stderr, "[archtype] at_renderer2d_begin called twice without end\n");
        return;
    }
    if (!renderer->frame_started) {
        fprintf(stderr, "[archtype] at_renderer2d_begin must be called between "
                        "at_renderer_begin_frame and at_renderer_end_frame\n");
        return;
    }

    /* First begin of a new frame: its GPU buffer is free again (begin_frame */
    /* waited for the GPU to finish the last frame that used it).            */
    if (renderer2d->frame_number != renderer->frame_number) {
        renderer2d->frame_number       = renderer->frame_number;
        renderer2d->frame_vertex_count = 0;
        renderer2d->frame_index_count  = 0;
        memset(&renderer2d->stats, 0, sizeof(renderer2d->stats));
    }

    renderer2d->staged_vertex_count = 0;
    renderer2d->staged_index_count  = 0;
    renderer2d->item_count          = 0;
    renderer2d->needs_sort          = false;
    renderer2d->layer               = 0;
    renderer2d->blend_mode          = AT_BLEND_ALPHA;

    at_camera2d default_camera = at_camera2d_make();
    if (!camera) {
        camera = &default_camera;
    }

    VkExtent2D extent = renderer->swapchain.extent;
    renderer2d->view_projection = at_camera2d_view_projection(camera, (float)extent.width,
                                                              (float)extent.height);
    renderer2d->zoom   = camera->zoom > 0.0f ? camera->zoom : 1.0f;
    renderer2d->active = true;
}

void at_renderer2d_end(at_renderer2d *renderer2d) {
    if (!renderer2d) {
        return;
    }
    if (!renderer2d->active) {
        fprintf(stderr, "[archtype] at_renderer2d_end called without begin\n");
        return;
    }

    submit(renderer2d);
    renderer2d->staged_vertex_count = 0;
    renderer2d->staged_index_count  = 0;
    renderer2d->item_count          = 0;
    renderer2d->active              = false;
}

/* ------------------------------------------------------------------------ */
/* Draw state                                                                */
/* ------------------------------------------------------------------------ */

void at_renderer2d_set_layer(at_renderer2d *renderer2d, int32_t layer) {
    if (renderer2d) {
        renderer2d->layer = layer;
    }
}

int32_t at_renderer2d_get_layer(const at_renderer2d *renderer2d) {
    return renderer2d ? renderer2d->layer : 0;
}

void at_renderer2d_set_blend_mode(at_renderer2d *renderer2d, at_blend_mode mode) {
    if (!renderer2d) {
        return;
    }
    if ((uint32_t)mode >= AT_BLEND_MODE_COUNT) {
        fprintf(stderr, "[archtype] at_renderer2d_set_blend_mode: unknown blend mode %d\n",
                (int)mode);
        return;
    }
    renderer2d->blend_mode = mode;
}

at_blend_mode at_renderer2d_get_blend_mode(const at_renderer2d *renderer2d) {
    return renderer2d ? renderer2d->blend_mode : AT_BLEND_ALPHA;
}

at_renderer2d_stats at_renderer2d_get_stats(const at_renderer2d *renderer2d) {
    at_renderer2d_stats stats;
    memset(&stats, 0, sizeof(stats));
    if (renderer2d) {
        stats = renderer2d->stats;
    }
    return stats;
}

/* ------------------------------------------------------------------------ */
/* Shapes                                                                    */
/* ------------------------------------------------------------------------ */

void at_renderer2d_draw_triangle(at_renderer2d *renderer2d,
                                 at_vec2 a, at_vec2 b, at_vec2 c, at_color color) {
    at_geometry2d g;
    if (!renderer2d || !reserve(renderer2d, 3, 3, renderer2d->white_texture, &g)) {
        return;
    }

    uint8_t rgba[4];
    pack_color(color, rgba);
    set_vertex(&g.vertices[0], a.x, a.y, WHITE_UV, WHITE_UV, rgba);
    set_vertex(&g.vertices[1], b.x, b.y, WHITE_UV, WHITE_UV, rgba);
    set_vertex(&g.vertices[2], c.x, c.y, WHITE_UV, WHITE_UV, rgba);

    g.indices[0] = g.base;
    g.indices[1] = g.base + 1;
    g.indices[2] = g.base + 2;
}

/* A quad: four corners given in order around the shape, as two triangles */
/* that share two of them. uvs holds the texture coordinates of each      */
/* corner, or NULL for the white texture of the shapes.                   */
static void draw_quad(at_renderer2d *renderer2d, at_texture *texture, const at_vec2 corners[4],
                      const at_vec2 uvs[4], at_color color) {
    at_geometry2d g;
    if (!reserve(renderer2d, 4, 6, texture, &g)) {
        return;
    }

    uint8_t rgba[4];
    pack_color(color, rgba);
    for (int i = 0; i < 4; ++i) {
        float u = uvs ? uvs[i].x : WHITE_UV;
        float v = uvs ? uvs[i].y : WHITE_UV;
        set_vertex(&g.vertices[i], corners[i].x, corners[i].y, u, v, rgba);
    }

    static const uint32_t order[6] = { 0, 1, 2, 2, 3, 0 };
    for (int i = 0; i < 6; ++i) {
        g.indices[i] = g.base + order[i];
    }
}

void at_renderer2d_draw_rect(at_renderer2d *renderer2d, at_vec2 center, at_vec2 size,
                             float rotation, at_color color) {
    if (!renderer2d) {
        return;
    }

    float hw = size.x * 0.5f;
    float hh = size.y * 0.5f;
    float c  = cosf(rotation);
    float s  = sinf(rotation);

    /* Corners around the center, then turned and moved into place */
    const float local[4][2] = { { -hw, -hh }, { hw, -hh }, { hw, hh }, { -hw, hh } };

    at_vec2 corners[4];
    for (int i = 0; i < 4; ++i) {
        corners[i] = at_vec2_make(center.x + local[i][0] * c - local[i][1] * s,
                                  center.y + local[i][0] * s + local[i][1] * c);
    }

    draw_quad(renderer2d, renderer2d->white_texture, corners, NULL, color);
}

void at_renderer2d_draw_circle(at_renderer2d *renderer2d, at_vec2 center, float radius,
                               at_color color) {
    if (!renderer2d || radius <= 0.0f) {
        return;
    }

    /* More segments for bigger circles on screen: about 6 * sqrt(radius) */
    float    screen_radius = radius * renderer2d->zoom;
    uint32_t segments      = (uint32_t)(6.0f * sqrtf(screen_radius));
    if (segments < AT_CIRCLE_MIN_SEGMENTS) {
        segments = AT_CIRCLE_MIN_SEGMENTS;
    } else if (segments > AT_CIRCLE_MAX_SEGMENTS) {
        segments = AT_CIRCLE_MAX_SEGMENTS;
    }

    /* A fan of triangles around one shared center vertex */
    at_geometry2d g;
    if (!reserve(renderer2d, segments + 1, segments * 3, renderer2d->white_texture, &g)) {
        return;
    }

    uint8_t rgba[4];
    pack_color(color, rgba);

    set_vertex(&g.vertices[0], center.x, center.y, WHITE_UV, WHITE_UV, rgba);

    float step = AT_TAU / (float)segments;
    for (uint32_t i = 0; i < segments; ++i) {
        float angle = step * (float)i;
        set_vertex(&g.vertices[1 + i], center.x + radius * cosf(angle),
                   center.y + radius * sinf(angle), WHITE_UV, WHITE_UV, rgba);

        /* Center, this point, next point (the last one closes the ring) */
        g.indices[i * 3 + 0] = g.base;
        g.indices[i * 3 + 1] = g.base + 1 + i;
        g.indices[i * 3 + 2] = g.base + 1 + (i + 1) % segments;
    }
}

void at_renderer2d_draw_line(at_renderer2d *renderer2d, at_vec2 from, at_vec2 to,
                             float thickness, at_color color) {
    if (!renderer2d || thickness <= 0.0f) {
        return;
    }

    float dx     = to.x - from.x;
    float dy     = to.y - from.y;
    float length = sqrtf(dx * dx + dy * dy);
    if (length <= AT_EPSILON) {
        return;
    }

    /* Half the thickness to each side, perpendicular to the line */
    float nx = -dy / length * thickness * 0.5f;
    float ny =  dx / length * thickness * 0.5f;

    at_vec2 corners[4] = {
        at_vec2_make(from.x - nx, from.y - ny),
        at_vec2_make(to.x   - nx, to.y   - ny),
        at_vec2_make(to.x   + nx, to.y   + ny),
        at_vec2_make(from.x + nx, from.y + ny),
    };

    draw_quad(renderer2d, renderer2d->white_texture, corners, NULL, color);
}

/* ------------------------------------------------------------------------ */
/* Sprites                                                                   */
/* ------------------------------------------------------------------------ */

void at_renderer2d_draw_sprite(at_renderer2d *renderer2d, at_texture *texture,
                               at_vec2 center, at_vec2 size, float rotation, at_color tint) {
    at_sprite_desc sprite = at_sprite_desc_make(texture, center);
    sprite.size     = size;
    sprite.rotation = rotation;
    sprite.tint     = tint;
    at_renderer2d_draw_sprite_ex(renderer2d, &sprite);
}

void at_renderer2d_draw_sprite_ex(at_renderer2d *renderer2d, const at_sprite_desc *sprite) {
    if (!renderer2d || !sprite) {
        return;
    }

    at_texture *texture = sprite->texture ? sprite->texture : renderer2d->white_texture;

    uint32_t texture_width  = 1;
    uint32_t texture_height = 1;
    at_texture_get_size(texture, &texture_width, &texture_height);

    /* Part of the texture to draw, in texels; empty = all of it */
    at_rect source = sprite->source;
    if (!sprite->texture || at_rect_is_empty(source)) {
        source = at_rect_make(0.0f, 0.0f, (float)texture_width, (float)texture_height);
    }

    /* (0, 0) means the source size: one texel per world unit */
    float width  = sprite->size.x;
    float height = sprite->size.y;
    if (width == 0.0f && height == 0.0f) {
        width  = source.width;
        height = source.height;
    }

    /* Edges around the origin point, with +Y up */
    float left   = -sprite->origin.x * width;
    float right  = (1.0f - sprite->origin.x) * width;
    float bottom = -sprite->origin.y * height;
    float top    = (1.0f - sprite->origin.y) * height;

    const float local[4][2] = {
        { left,  bottom }, { right, bottom }, { right, top }, { left, top },
    };

    float   c = cosf(sprite->rotation);
    float   s = sinf(sprite->rotation);
    at_vec2 corners[4];
    for (int i = 0; i < 4; ++i) {
        corners[i] = at_vec2_make(sprite->position.x + local[i][0] * c - local[i][1] * s,
                                  sprite->position.y + local[i][0] * s + local[i][1] * c);
    }

    /* Texture coordinates of the source (0..1 over the whole texture).  */
    /* (0, 0) is the top-left of the image, so the top of the sprite     */
    /* (+Y in the world) uses the top of the source.                     */
    float left_u   = source.x / (float)texture_width;
    float right_u  = (source.x + source.width) / (float)texture_width;
    float top_v    = source.y / (float)texture_height;
    float bottom_v = (source.y + source.height) / (float)texture_height;

    float u0 = sprite->flip_x ? right_u  : left_u;
    float u1 = sprite->flip_x ? left_u   : right_u;
    float v0 = sprite->flip_y ? bottom_v : top_v;    /* Top of the sprite    */
    float v1 = sprite->flip_y ? top_v    : bottom_v; /* Bottom of the sprite */

    const at_vec2 uvs[4] = {
        at_vec2_make(u0, v1), at_vec2_make(u1, v1), at_vec2_make(u1, v0), at_vec2_make(u0, v0),
    };

    draw_quad(renderer2d, texture, corners, uvs, sprite->tint);
}