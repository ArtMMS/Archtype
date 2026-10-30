#ifndef ARCHTYPE_RENDERER_PIPELINE_H
#define ARCHTYPE_RENDERER_PIPELINE_H

#include "archtype/core/defines.h"
#include "archtype/renderer/renderer.h"
#include "archtype/renderer/shader.h"

AT_EXTERN_C_BEGIN

#define AT_MAX_VERTEX_ATTRIBUTES 8

/* How vertices are assembled into primitives. */
typedef enum at_primitive_topology {
    AT_PRIMITIVE_TRIANGLES = 0,    /* Every 3 vertices form a triangle (default) */
    AT_PRIMITIVE_TRIANGLE_STRIP,   /* Each new vertex forms a triangle with the previous 2 */
    AT_PRIMITIVE_LINES,            /* Every 2 vertices form a line */
    AT_PRIMITIVE_LINE_STRIP,       /* Connected lines */
    AT_PRIMITIVE_POINTS            /* One point per vertex */
} at_primitive_topology;

/* Which triangle faces are skipped. */
typedef enum at_cull_mode {
    AT_CULL_NONE = 0,              /* Draw both faces (default) */
    AT_CULL_BACK,                  /* Skip back faces (usual for closed 3D models) */
    AT_CULL_FRONT                  /* Skip front faces */
} at_cull_mode;

/* Which vertex order, as seen on screen, makes a triangle face the viewer. */
typedef enum at_front_face {
    AT_FRONT_FACE_CCW = 0,         /* Counter-clockwise (default, same as glTF/Blender) */
    AT_FRONT_FACE_CW               /* Clockwise */
} at_front_face;

/* How new pixels are combined with what is already on screen. */
typedef enum at_blend_mode {
    AT_BLEND_NONE = 0,             /* Replace (opaque, default) */
    AT_BLEND_ALPHA,                /* Standard transparency using alpha */
    AT_BLEND_ADDITIVE              /* Add colors (glows, particles, light) */
} at_blend_mode;

/* Type of one vertex attribute, as read by the vertex shader. */
typedef enum at_vertex_format {
    AT_VERTEX_FORMAT_FLOAT = 0,    /* float  -> float                         */
    AT_VERTEX_FORMAT_FLOAT2,       /* float[2] -> vec2                        */
    AT_VERTEX_FORMAT_FLOAT3,       /* float[3] -> vec3                        */
    AT_VERTEX_FORMAT_FLOAT4,       /* float[4] -> vec4                        */
    AT_VERTEX_FORMAT_UBYTE4_NORM   /* uint8_t[4] (0..255) -> vec4 (0..1)      */
} at_vertex_format;

/* One field of the vertex struct, e.g. the position or the color. */
typedef struct at_vertex_attribute {
    uint32_t         location;     /* layout(location = N) in the vertex shader */
    at_vertex_format format;
    uint32_t         offset;       /* offsetof(your_vertex, field)              */
} at_vertex_attribute;

/* Describes the vertex struct stored in the vertex buffer.             */
/* Leave it zeroed when the shader builds its vertices itself.          */
typedef struct at_vertex_layout {
    uint32_t            stride;          /* sizeof(your_vertex) */
    uint32_t            attribute_count;
    at_vertex_attribute attributes[AT_MAX_VERTEX_ATTRIBUTES];
} at_vertex_layout;

/* Zero-initialize and fill only what you need: the defaults draw opaque, */
/* unculled triangles without depth testing.                              */
typedef struct at_pipeline_desc {
    at_shader            *vertex_shader;   /* Required */
    at_shader            *fragment_shader; /* Required */
    at_vertex_layout      vertex_layout;

    at_primitive_topology topology;
    at_cull_mode          cull_mode;
    at_front_face         front_face;
    at_blend_mode         blend_mode;

    /* Depth testing: takes effect once the renderer has a depth buffer. */
    bool                  depth_test;      /* Hide pixels behind closer ones   */
    bool                  depth_write;     /* Store this draw's depth          */
} at_pipeline_desc;

/* Creates a graphics pipeline. Returns NULL on failure. */
AT_API at_pipeline *at_pipeline_create(at_renderer *renderer, const at_pipeline_desc *desc);

/* Destroys a pipeline. Passing NULL is allowed.                 */
/* Waits for the GPU, so avoid calling it every frame.           */
AT_API void at_pipeline_destroy(at_renderer *renderer, at_pipeline *pipeline);

AT_EXTERN_C_END

#endif /* ARCHTYPE_RENDERER_PIPELINE_H */