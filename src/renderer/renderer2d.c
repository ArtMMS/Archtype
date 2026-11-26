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

/* Must match the inputs of renderer2d.vert */
typedef struct at_vertex2d {
    float   position[2];
    float   uv[2];
    uint8_t color[4]; /* sRGB, 0..255 */
} at_vertex2d;

struct at_renderer2d {
    at_renderer  *renderer;
    at_pipeline  *pipeline;
    at_texture   *white_texture; /* Lets shapes use the same shader as sprites */

    /* One vertex buffer per frame in flight, written by the CPU while the  */
    /* GPU reads the other one.                                            */
    at_vk_buffer  vertex_buffers[AT_VK_FRAMES_IN_FLIGHT];
    uint32_t      max_vertices;

    /* Vertices of the current frame */
    uint64_t      frame_number;  /* Renderer frame the counts below belong to */
    uint32_t      vertex_count;  /* Vertices written this frame               */
    uint32_t      batch_start;   /* First vertex not sent to the GPU yet      */
    at_texture   *batch_texture; /* Texture of the vertices not sent yet      */
    bool          full_warned;   /* "Buffer full" is only reported once       */

    /* Between begin and end */
    bool          active;
    bool          inactive_warned;
    at_mat4       view_projection;
    float         zoom;
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

static at_vk_buffer *current_vertex_buffer(at_renderer2d *renderer2d) {
    return &renderer2d->vertex_buffers[at_renderer_frame_index(renderer2d->renderer)];
}

static void flush(at_renderer2d *renderer2d);

/* Takes room for count vertices that use texture in this frame's buffer.   */
/* A different texture than the vertices waiting to be drawn sends those to */
/* the GPU first. Returns NULL (and the drawing is skipped) outside         */
/* begin/end or when the buffer is full.                                    */
static at_vertex2d *reserve_vertices(at_renderer2d *renderer2d, uint32_t count,
                                     at_texture *texture) {
    if (!renderer2d->active) {
        if (!renderer2d->inactive_warned) {
            fprintf(stderr, "[archtype] 2D shapes and sprites must be drawn between "
                            "at_renderer2d_begin and at_renderer2d_end\n");
            renderer2d->inactive_warned = true;
        }
        return NULL;
    }

    if (count > renderer2d->max_vertices - renderer2d->vertex_count) {
        if (!renderer2d->full_warned) {
            fprintf(stderr, "[archtype] 2D renderer is full (%u vertices this frame): "
                            "drawing skipped. Raise at_renderer2d_desc.max_vertices\n",
                    renderer2d->max_vertices);
            renderer2d->full_warned = true;
        }
        return NULL;
    }

    /* One batch is one texture: a new texture ends the current batch */
    if (texture != renderer2d->batch_texture) {
        flush(renderer2d);
        renderer2d->batch_texture = texture;
    }

    at_vertex2d *vertices = (at_vertex2d *)current_vertex_buffer(renderer2d)->mapped
                          + renderer2d->vertex_count;
    renderer2d->vertex_count += count;
    return vertices;
}

/* Draws the vertices written since the last flush with one draw call, */
/* using the batch's texture.                                           */
static void flush(at_renderer2d *renderer2d) {
    uint32_t count = renderer2d->vertex_count - renderer2d->batch_start;
    if (count == 0) {
        return;
    }

    at_renderer  *renderer = renderer2d->renderer;
    at_vk_buffer *buffer   = current_vertex_buffer(renderer2d);
    VkDeviceSize  offset   = (VkDeviceSize)renderer2d->batch_start * sizeof(at_vertex2d);

    /* Make the CPU writes visible to the GPU (no-op on coherent memory) */
    at_vk_buffer_flush(buffer, &renderer->allocator, offset,
                       (VkDeviceSize)count * sizeof(at_vertex2d));

    at_renderer_bind_pipeline(renderer, renderer2d->pipeline);
    at_renderer_bind_texture(renderer, 0, renderer2d->batch_texture);
    at_renderer_push_constants(renderer, &renderer2d->view_projection, sizeof(at_mat4));

    /* The batch starts in the middle of the buffer: bind it at an offset */
    vkCmdBindVertexBuffers(at_renderer_command_buffer(renderer), 0, 1, &buffer->handle, &offset);
    at_renderer_draw(renderer, count);

    renderer2d->batch_start = renderer2d->vertex_count;
}

/* ------------------------------------------------------------------------ */
/* Creation and destruction                                                  */
/* ------------------------------------------------------------------------ */

static bool create_pipeline(at_renderer2d *renderer2d) {
    at_renderer *renderer = renderer2d->renderer;

    at_shader *vertex_shader   = at_shader_create(renderer, k_vertex_shader,
                                                  sizeof(k_vertex_shader));
    at_shader *fragment_shader = at_shader_create(renderer, k_fragment_shader,
                                                  sizeof(k_fragment_shader));

    if (vertex_shader && fragment_shader) {
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
        desc.cull_mode          = AT_CULL_NONE;   /* Shapes can be given in any order */
        desc.blend_mode         = AT_BLEND_ALPHA; /* Respect the colors' alpha        */

        renderer2d->pipeline = at_pipeline_create(renderer, &desc);
    }

    /* The pipeline keeps what it needs */
    at_shader_destroy(renderer, vertex_shader);
    at_shader_destroy(renderer, fragment_shader);
    return renderer2d->pipeline != NULL;
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

static bool create_vertex_buffers(at_renderer2d *renderer2d) {
    VkDeviceSize size = (VkDeviceSize)renderer2d->max_vertices * sizeof(at_vertex2d);

    for (uint32_t i = 0; i < AT_VK_FRAMES_IN_FLIGHT; ++i) {
        if (!at_vk_buffer_create_host(&renderer2d->vertex_buffers[i],
                                      &renderer2d->renderer->allocator, size,
                                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
            return false;
        }
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
    renderer2d->zoom         = 1.0f;

    if (!create_pipeline(renderer2d) || !create_white_texture(renderer2d) ||
        !create_vertex_buffers(renderer2d)) {
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

    /* The GPU may still be reading the vertex buffers */
    vkDeviceWaitIdle(renderer->context.device);

    for (uint32_t i = 0; i < AT_VK_FRAMES_IN_FLIGHT; ++i) {
        at_vk_buffer_destroy(&renderer2d->vertex_buffers[i], &renderer->allocator);
    }
    at_texture_destroy(renderer, renderer2d->white_texture);
    at_pipeline_destroy(renderer, renderer2d->pipeline);
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

    /* First begin of a new frame: its buffer is free again (begin_frame  */
    /* waited for the GPU to finish the last frame that used it).         */
    if (renderer2d->frame_number != renderer->frame_number) {
        renderer2d->frame_number = renderer->frame_number;
        renderer2d->vertex_count = 0;
        renderer2d->batch_start  = 0;
    }

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

    flush(renderer2d);
    renderer2d->batch_texture = NULL;
    renderer2d->active        = false;
}

/* ------------------------------------------------------------------------ */
/* Shapes                                                                    */
/* ------------------------------------------------------------------------ */

void at_renderer2d_draw_triangle(at_renderer2d *renderer2d,
                                 at_vec2 a, at_vec2 b, at_vec2 c, at_color color) {
    if (!renderer2d) {
        return;
    }

    at_vertex2d *v = reserve_vertices(renderer2d, 3, renderer2d->white_texture);
    if (!v) {
        return;
    }

    uint8_t rgba[4];
    pack_color(color, rgba);
    set_vertex(&v[0], a.x, a.y, WHITE_UV, WHITE_UV, rgba);
    set_vertex(&v[1], b.x, b.y, WHITE_UV, WHITE_UV, rgba);
    set_vertex(&v[2], c.x, c.y, WHITE_UV, WHITE_UV, rgba);
}

/* Two triangles from four corners given in order around the shape.  */
/* uvs holds the texture coordinates of each corner, or NULL for the  */
/* white texture of the shapes.                                       */
static void write_quad(at_vertex2d *v, const at_vec2 corners[4], const at_vec2 uvs[4],
                       const uint8_t rgba[4]) {
    static const int order[6] = { 0, 1, 2, 2, 3, 0 };
    for (int i = 0; i < 6; ++i) {
        int   corner = order[i];
        float u      = uvs ? uvs[corner].x : WHITE_UV;
        float t      = uvs ? uvs[corner].y : WHITE_UV;
        set_vertex(&v[i], corners[corner].x, corners[corner].y, u, t, rgba);
    }
}

void at_renderer2d_draw_rect(at_renderer2d *renderer2d, at_vec2 center, at_vec2 size,
                             float rotation, at_color color) {
    if (!renderer2d) {
        return;
    }

    at_vertex2d *v = reserve_vertices(renderer2d, 6, renderer2d->white_texture);
    if (!v) {
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

    uint8_t rgba[4];
    pack_color(color, rgba);
    write_quad(v, corners, NULL, rgba);
}

void at_renderer2d_draw_circle(at_renderer2d *renderer2d, at_vec2 center, float radius,
                               at_color color) {
    if (!renderer2d || radius <= 0.0f) {
        return;
    }

    /* More segments for bigger circles on screen: about 6 * sqrt(radius) */
    float screen_radius = radius * renderer2d->zoom;
    int   segments      = (int)(6.0f * sqrtf(screen_radius));
    if (segments < AT_CIRCLE_MIN_SEGMENTS) {
        segments = AT_CIRCLE_MIN_SEGMENTS;
    } else if (segments > AT_CIRCLE_MAX_SEGMENTS) {
        segments = AT_CIRCLE_MAX_SEGMENTS;
    }

    at_vertex2d *v = reserve_vertices(renderer2d, (uint32_t)segments * 3,
                                      renderer2d->white_texture);
    if (!v) {
        return;
    }

    uint8_t rgba[4];
    pack_color(color, rgba);

    /* A fan of triangles from the center */
    float step   = AT_TAU / (float)segments;
    float prev_x = center.x + radius;
    float prev_y = center.y;

    for (int i = 1; i <= segments; ++i) {
        float angle = step * (float)i;
        float x     = i == segments ? center.x + radius : center.x + radius * cosf(angle);
        float y     = i == segments ? center.y          : center.y + radius * sinf(angle);

        set_vertex(&v[0], center.x, center.y, WHITE_UV, WHITE_UV, rgba);
        set_vertex(&v[1], prev_x, prev_y, WHITE_UV, WHITE_UV, rgba);
        set_vertex(&v[2], x, y, WHITE_UV, WHITE_UV, rgba);
        v += 3;

        prev_x = x;
        prev_y = y;
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

    at_vertex2d *v = reserve_vertices(renderer2d, 6, renderer2d->white_texture);
    if (!v) {
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

    uint8_t rgba[4];
    pack_color(color, rgba);
    write_quad(v, corners, NULL, rgba);
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

    /* (0, 0) means the texture size: one texel per world unit */
    float width  = sprite->size.x;
    float height = sprite->size.y;
    if (width == 0.0f && height == 0.0f) {
        uint32_t texture_width  = 1;
        uint32_t texture_height = 1;
        if (sprite->texture) {
            at_texture_get_size(sprite->texture, &texture_width, &texture_height);
        }
        width  = (float)texture_width;
        height = (float)texture_height;
    }

    at_vertex2d *v = reserve_vertices(renderer2d, 6, texture);
    if (!v) {
        return;
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

    /* Texture coordinates: (0, 0) is the top-left of the image, so the  */
    /* top of the sprite (+Y in the world) uses v = 0.                   */
    float u0 = sprite->flip_x ? 1.0f : 0.0f;
    float u1 = sprite->flip_x ? 0.0f : 1.0f;
    float v0 = sprite->flip_y ? 1.0f : 0.0f; /* Top    */
    float v1 = sprite->flip_y ? 0.0f : 1.0f; /* Bottom */

    const at_vec2 uvs[4] = {
        at_vec2_make(u0, v1), at_vec2_make(u1, v1), at_vec2_make(u1, v0), at_vec2_make(u0, v0),
    };

    uint8_t rgba[4];
    pack_color(sprite->tint, rgba);
    write_quad(v, corners, uvs, rgba);
}