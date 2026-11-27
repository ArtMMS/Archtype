#ifndef ARCHTYPE_RENDERER_MESH_H
#define ARCHTYPE_RENDERER_MESH_H

#include "archtype/core/defines.h"
#include "archtype/math/vec.h"
#include "archtype/renderer/pipeline.h"
#include "archtype/renderer/renderer.h"

AT_EXTERN_C_BEGIN

/*
 * 3D meshes: triangles made of the engine's standard vertex (at_vertex3d).
 *
 * Two levels:
 *   - at_mesh_data: vertices and indices in CPU memory. The generators
 *     below (cube, plane, sphere) fill one; it can also be filled by hand.
 *     Free it with at_mesh_data_free.
 *   - at_mesh: the same data uploaded to the GPU, ready to draw.
 *
 * Conventions (the same as the rest of the engine):
 *   - Right-handed, +Y up. Triangles go counter-clockwise seen from
 *     outside, so closed meshes can be drawn with AT_CULL_BACK.
 *   - Texture coordinates: (0, 0) is the top-left of the image, u goes
 *     right and v goes down.
 *   - Tangent space (for normal maps): tangent.xyz points where u grows,
 *     and the bitangent, cross(normal, tangent.xyz) * tangent.w, points up
 *     in the image (where v shrinks). This is the convention of glTF and
 *     of most normal maps ("OpenGL style", green up).
 */

/* The vertex of every 3D mesh: 48 bytes. */
typedef struct at_vertex3d {
    float position[3];
    float normal[3];  /* Unit length, pointing out of the surface       */
    float uv[2];      /* Texture coordinates                            */
    float tangent[4]; /* xyz: unit length, along +u. w: +1 or -1, the   */
                      /* side of the bitangent (see above)              */
} at_vertex3d;

/* Vertex layout of at_vertex3d, for at_pipeline_desc.vertex_layout:     */
/*   location 0: vec3 position        location 2: vec2 uv                */
/*   location 1: vec3 normal          location 3: vec4 tangent           */
static inline at_vertex_layout at_mesh_vertex_layout(void) {
    at_vertex_layout layout = { 0 };
    layout.stride          = (uint32_t)sizeof(at_vertex3d);
    layout.attribute_count = 4;

    layout.attributes[0].location = 0;
    layout.attributes[0].format   = AT_VERTEX_FORMAT_FLOAT3;
    layout.attributes[0].offset   = (uint32_t)offsetof(at_vertex3d, position);

    layout.attributes[1].location = 1;
    layout.attributes[1].format   = AT_VERTEX_FORMAT_FLOAT3;
    layout.attributes[1].offset   = (uint32_t)offsetof(at_vertex3d, normal);

    layout.attributes[2].location = 2;
    layout.attributes[2].format   = AT_VERTEX_FORMAT_FLOAT2;
    layout.attributes[2].offset   = (uint32_t)offsetof(at_vertex3d, uv);

    layout.attributes[3].location = 3;
    layout.attributes[3].format   = AT_VERTEX_FORMAT_FLOAT4;
    layout.attributes[3].offset   = (uint32_t)offsetof(at_vertex3d, tangent);
    return layout;
}

/* ------------------------------------------------------------------------ */
/* Mesh data (CPU)                                                           */
/* ------------------------------------------------------------------------ */

/* Vertices and indices in CPU memory. Every 3 indices form a triangle. */
typedef struct at_mesh_data {
    at_vertex3d *vertices;
    uint32_t     vertex_count;
    uint32_t    *indices;
    uint32_t     index_count;
} at_mesh_data;

/* The generators overwrite *data (it doesn't need to be initialized) and */
/* return false on invalid sizes or when out of memory, leaving it empty. */
/* Free the result with at_mesh_data_free.                                */

/* Box centered on the origin, size.x wide, size.y tall and size.z deep. */
/* Each face has its own 4 vertices (so its own normal) and the whole    */
/* texture: 24 vertices, 36 indices.                                     */
AT_API bool at_mesh_data_cube(at_mesh_data *data, at_vec3 size);

/* Flat floor on the XZ plane, centered on the origin and facing +Y:     */
/* width along X, depth along Z, cut into subdivisions_x by              */
/* subdivisions_z squares (1 to 4096 each; 0 is raised to 1). The        */
/* texture covers it once, with its top toward -Z.                       */
AT_API bool at_mesh_data_plane(at_mesh_data *data, float width, float depth,
                               uint32_t subdivisions_x, uint32_t subdivisions_z);

/* UV sphere centered on the origin: segments around the Y axis (3 to   */
/* 4096; smaller values are raised to 3) and rings from pole to pole    */
/* (2 to 4096; raised to 2). The texture wraps around it once, with its */
/* seam at the back (-Z) and its top at the north pole (+Y).            */
AT_API bool at_mesh_data_sphere(at_mesh_data *data, float radius, uint32_t segments,
                                uint32_t rings);

/* Frees the vertices and indices and empties *data (safe to call twice). */
AT_API void at_mesh_data_free(at_mesh_data *data);

/* ------------------------------------------------------------------------ */
/* Meshes (GPU)                                                              */
/* ------------------------------------------------------------------------ */

/* Opaque mesh handle: vertex and index buffers in fast GPU memory. */
typedef struct at_mesh at_mesh;

/* Uploads mesh data to the GPU. The data can be freed right after.  */
/* Returns NULL on failure, e.g. an index past the last vertex.      */
AT_API at_mesh *at_mesh_create(at_renderer *renderer, const at_mesh_data *data);

/* Shortcuts: generate the data, upload it and free it. NULL on failure. */
AT_API at_mesh *at_mesh_create_cube(at_renderer *renderer, at_vec3 size);
AT_API at_mesh *at_mesh_create_plane(at_renderer *renderer, float width, float depth,
                                     uint32_t subdivisions_x, uint32_t subdivisions_z);
AT_API at_mesh *at_mesh_create_sphere(at_renderer *renderer, float radius, uint32_t segments,
                                      uint32_t rings);

/* Destroys a mesh. Passing NULL is allowed.           */
/* Waits for the GPU, so avoid calling it every frame. */
AT_API void at_mesh_destroy(at_renderer *renderer, at_mesh *mesh);

AT_API uint32_t at_mesh_get_vertex_count(const at_mesh *mesh);
AT_API uint32_t at_mesh_get_index_count(const at_mesh *mesh);

/* Binds the mesh's vertex and index buffers and draws all of it. Only    */
/* valid between begin_frame and end_frame, with a pipeline bound whose   */
/* vertex layout is at_mesh_vertex_layout(). The buffers stay bound.      */
AT_API void at_mesh_draw(at_renderer *renderer, const at_mesh *mesh);

AT_EXTERN_C_END

#endif /* ARCHTYPE_RENDERER_MESH_H */