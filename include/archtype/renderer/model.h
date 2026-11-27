#ifndef ARCHTYPE_RENDERER_MODEL_H
#define ARCHTYPE_RENDERER_MODEL_H

#include "archtype/core/defines.h"
#include "archtype/math/mat4.h"
#include "archtype/math/vec.h"
#include "archtype/renderer/material.h"
#include "archtype/renderer/mesh.h"
#include "archtype/renderer/renderer.h"
#include "archtype/renderer/renderer3d.h"

AT_EXTERN_C_BEGIN

/*
 * 3D models loaded from files: one or more meshes ("parts"), each with a
 * material. Supported formats: Wavefront OBJ, with its MTL materials.
 *
 * There are two levels, like meshes:
 *
 *   at_model_data  the file's contents on the CPU: meshes, materials and
 *                  the paths of their textures. No GPU needed, so it can be
 *                  inspected, changed or tested.
 *   at_model       ready to draw: meshes on the GPU and the textures loaded.
 *
 * Usage:
 *
 *   at_model *crate = at_model_load(renderer, "models/crate.obj");
 *   ...
 *   at_renderer3d_begin(r3d, &camera);
 *   at_model_draw(r3d, crate, at_transform_to_mat4(transform));
 *   at_renderer3d_end(r3d);
 *   ...
 *   at_model_destroy(renderer, crate);
 *
 * OBJ files:
 *   - Read: v, vt, vn, f (v, v/vt, v//vn and v/vt/vn, negative indices
 *     too; faces with more than 3 corners are split into triangles), o, g,
 *     usemtl and mtllib. Everything else (s, l, p, ...) is ignored.
 *   - Each object, group or material change starts a new part. Corners
 *     that repeat the same position, UV and normal become one vertex.
 *   - Faces must be counter-clockwise seen from the front (the OBJ
 *     standard, and what Blender exports).
 *   - Texture coordinates are flipped (v = 1 - v): OBJ puts (0, 0) at the
 *     bottom-left of the image, the engine at the top-left.
 *   - Parts without normals get smooth ones (at_mesh_data_compute_normals);
 *     every part gets tangents (at_mesh_data_compute_tangents).
 *
 * MTL materials (converted to at_material):
 *   Kd -> base_color   Ks -> specular (average)   Ns -> shininess
 *   Ke -> emissive     d / Tr -> alpha (below 1 makes it transparent)
 *   map_Kd -> base texture   map_Bump, bump, norm -> normal texture
 *   Kd and Ke are linear in MTL files (as Blender writes them), so they are
 *   converted to sRGB, the space of every engine color. Texture options
 *   (-s, -o, -bm, ...) are skipped.
 *
 * Paths: mtllib and texture paths are relative to the file that names
 * them. A relative model path is resolved from the executable's folder,
 * like every asset.
 */

#define AT_MODEL_NAME_MAX 64
#define AT_MODEL_PATH_MAX 512

/* ------------------------------------------------------------------------ */
/* Model data (CPU)                                                          */
/* ------------------------------------------------------------------------ */

typedef struct at_model_material_data {
    char        name[AT_MODEL_NAME_MAX];
    at_material material;  /* base_texture is NULL: the textures are paths */
    char        base_texture_path[AT_MODEL_PATH_MAX];   /* "" if none      */
    char        normal_texture_path[AT_MODEL_PATH_MAX]; /* "" if none      */
} at_model_material_data;

typedef struct at_model_part_data {
    char         name[AT_MODEL_NAME_MAX]; /* Object or group name, may be "" */
    at_mesh_data mesh;
    uint32_t     material;                /* Index into materials           */
} at_model_part_data;

typedef struct at_model_data {
    at_model_part_data     *parts;
    uint32_t                part_count;
    at_model_material_data *materials; /* Parts without a known material use */
    uint32_t                material_count; /* one named "default"            */
} at_model_data;

/* Loads an OBJ file and the MTL files it names. Returns false (logging */
/* why, with the line number) on failure, or if it has no faces. Free   */
/* the result with at_model_data_free.                                  */
AT_API bool at_model_data_load_obj(at_model_data *data, const char *path);

/* The same from text in memory: an OBJ and, optionally, the MTL it uses  */
/* (mtl may be NULL; mtllib lines are then ignored). Texture paths are    */
/* kept as written.                                                       */
AT_API bool at_model_data_parse_obj(at_model_data *data, const char *obj, size_t obj_size,
                                    const char *mtl, size_t mtl_size);

/* Frees the data. Passing NULL or data that failed to load is allowed. */
AT_API void at_model_data_free(at_model_data *data);

/* ------------------------------------------------------------------------ */
/* Model (GPU)                                                               */
/* ------------------------------------------------------------------------ */

/* Opaque model handle. */
typedef struct at_model at_model;

/* Loads a model file (by its extension: .obj). Returns NULL on failure. */
AT_API at_model *at_model_load(at_renderer *renderer, const char *path);

/* Creates a model from data, loading its textures (sRGB, with mipmaps;   */
/* each file once). A texture that fails to load is left out, with a      */
/* warning. The data can be freed afterwards. Returns NULL on failure.    */
AT_API at_model *at_model_create(at_renderer *renderer, const at_model_data *data);

/* Destroys the model, its meshes and its textures. Passing NULL is   */
/* allowed. Waits for the GPU, so avoid calling it every frame.        */
AT_API void at_model_destroy(at_renderer *renderer, at_model *model);

/* Draws every part, placed in the world by transform. Only valid between */
/* at_renderer3d_begin and at_renderer3d_end.                             */
AT_API void at_model_draw(at_renderer3d *renderer3d, const at_model *model, at_mat4 transform);

AT_API uint32_t at_model_get_part_count(const at_model *model);
AT_API uint32_t at_model_get_material_count(const at_model *model);
AT_API uint32_t at_model_get_triangle_count(const at_model *model);

/* A material of the model, to change it (e.g. its color or texture).  */
/* NULL if index is out of range. Textures set here are not destroyed  */
/* with the model.                                                     */
AT_API at_material *at_model_get_material(at_model *model, uint32_t index);

/* Box around every vertex, in the model's own space. */
AT_API void at_model_get_bounds(const at_model *model, at_vec3 *min, at_vec3 *max);

AT_EXTERN_C_END

#endif /* ARCHTYPE_RENDERER_MODEL_H */