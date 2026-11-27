#ifndef ARCHTYPE_RENDERER_MESH_UTILS_H
#define ARCHTYPE_RENDERER_MESH_UTILS_H

#include "archtype/core/defines.h"
#include "archtype/renderer/mesh.h"

AT_EXTERN_C_BEGIN

/*
 * Tools for mesh data on the CPU (at_mesh_data), for meshes that don't come
 * from the engine's generators: loaded from files or built by code. The
 * model loaders use them when a file leaves something out.
 *
 * Both work on the triangles of the index buffer (every 3 indices make a
 * triangle, counter-clockwise seen from its front) and overwrite what was
 * there before. Triangles with no area are skipped.
 */

/* Smooth normals: each vertex gets the average of the normals of the     */
/* triangles around it, weighted by their area. Vertices at the same      */
/* position (within rounding errors) share their normal, even when they   */
/* are split by a UV seam, so the surface looks smooth across it. Hard    */
/* edges need their own normals. Vertices of no triangle (or of triangles */
/* with no area only) point up.                                           */
AT_API void at_mesh_data_compute_normals(at_mesh_data *data);

/* Tangents for normal maps, from the normals and the texture coordinates: */
/* xyz points to +u along the surface, perpendicular to the normal, and w  */
/* (1 or -1) makes cross(normal, tangent) * w point up in the image (to    */
/* -v), the convention of the engine's meshes. Normals must be set first.  */
/* Where the texture coordinates don't give a direction (no UVs, or all    */
/* the same), any direction perpendicular to the normal is used.           */
AT_API void at_mesh_data_compute_tangents(at_mesh_data *data);

AT_EXTERN_C_END

#endif /* ARCHTYPE_RENDERER_MESH_UTILS_H */