#ifndef ARCHTYPE_RENDERER_RENDERER3D_H
#define ARCHTYPE_RENDERER_RENDERER3D_H

#include "archtype/core/defines.h"
#include "archtype/math/mat4.h"
#include "archtype/renderer/camera3d.h"
#include "archtype/renderer/color.h"
#include "archtype/renderer/light.h"
#include "archtype/renderer/material.h"
#include "archtype/renderer/mesh.h"
#include "archtype/renderer/renderer.h"

AT_EXTERN_C_BEGIN

/*
 * 3D renderer: draws meshes in world space, seen through an at_camera3d,
 * lit by directional, point and spot lights, with the engine's built-in
 * shaders. Games don't write shaders or pipelines for it.
 *
 * Nothing is drawn right away: meshes and lights are collected between
 * begin and end, and at_renderer3d_end draws them all.
 *
 * Usage, every frame:
 *
 *   if (at_renderer_begin_frame(renderer)) {
 *       at_renderer3d_begin(r3d, &camera);
 *
 *       at_point_light lamp = at_point_light_make(at_vec3_make(0, 3, 0),
 *                                                 at_color_rgb(1, 0.8f, 0.6f),
 *                                                 10.0f, 12.0f);
 *       at_renderer3d_add_point_light(r3d, &lamp);
 *
 *       at_renderer3d_draw_mesh(r3d, cube, at_transform_to_mat4(transform),
 *                               at_color_rgb(1, 0, 0));
 *
 *       at_material glass = at_material_make();
 *       glass.base_color  = at_color_rgba(0.6f, 0.8f, 1.0f, 0.3f);
 *       glass.transparent = true;
 *       at_renderer3d_draw_mesh_ex(r3d, sphere, model, &glass);
 *
 *       at_renderer3d_end(r3d);
 *
 *       at_renderer2d_begin(r2d, &hud_camera);   (optional: a HUD on top)
 *       ...
 *       at_renderer2d_end(r2d);
 *       at_renderer_end_frame(renderer);
 *   }
 *
 * Lights: added between begin and end, like meshes, and they light every
 * mesh of that pass (up to the limits below; the rest are ignored). The
 * ambient light (at_renderer3d_set_ambient) is a setting that stays until
 * changed. Until the first light is added to a 3D renderer, each of its
 * passes gets a default sun, so meshes are never left in the dark; after
 * that, a pass without lights only has the ambient light.
 *
 * Draw order: opaque meshes first, sorted to change pipelines and
 * textures as little as possible; they test and write the depth buffer,
 * so they hide each other correctly in any order. Then transparent
 * meshes, from the farthest to the nearest (by the position of each
 * object's origin), so each one blends over what is behind it. They test
 * the depth buffer but don't write it. Transparent objects that cross
 * each other can still look wrong: sorting is per object, not per pixel.
 *
 * The 2D renderer doesn't test depth, so 2D drawn after at_renderer3d_end
 * goes on top, like a HUD.
 *
 * Faces: back faces are skipped, unless the material is double_sided.
 * Meshes that aren't double-sided must be closed, or seen from their
 * front side (like a floor plane seen from above).
 *
 * Textures: for 3D, create them with at_texture_options.mipmaps, so they
 * don't shimmer when seen far away or at an angle.
 *
 * begin/end can be used up to AT_RENDERER3D_MAX_PASSES times per frame,
 * e.g. with different cameras. Every pass shares the frame's depth buffer.
 *
 * at_renderer3d_end binds its own pipelines and buffers: after it, bind
 * your pipeline and buffers again before calling at_renderer_draw or
 * at_renderer_draw_indexed.
 *
 * Meshes and textures must stay alive until at_renderer3d_end.
 */

/* Opaque 3D renderer handle. */
typedef struct at_renderer3d at_renderer3d;

#define AT_RENDERER3D_MAX_PASSES             4  /* begin/end pairs per frame */
#define AT_RENDERER3D_MAX_DIRECTIONAL_LIGHTS 4  /* Per pass                  */
#define AT_RENDERER3D_MAX_POINT_LIGHTS       32 /* Per pass                  */
#define AT_RENDERER3D_MAX_SPOT_LIGHTS        16 /* Per pass                  */

/* What the 3D renderer did in the current frame, over every begin/end. */
/* Read it after at_renderer3d_end; it starts again at the first begin  */
/* of the next frame.                                                   */
typedef struct at_renderer3d_stats {
    uint32_t draw_calls;  /* Meshes drawn (one draw call each)        */
    uint32_t transparent; /* How many of them were transparent        */
    uint32_t triangles;
    uint32_t lights;      /* Lights used, the default sun included    */
} at_renderer3d_stats;

/* Creates a 3D renderer. Returns NULL on failure. */
AT_API at_renderer3d *at_renderer3d_create(at_renderer *renderer);

/* Destroys the 3D renderer. Passing NULL is allowed. Destroy it before the */
/* renderer it was created with. Waits for the GPU.                         */
AT_API void at_renderer3d_destroy(at_renderer3d *renderer3d);

/* Starts collecting meshes and lights seen through camera (NULL uses  */
/* at_camera3d_make()). The camera is copied: changing it afterwards   */
/* doesn't affect this pass. Only valid between at_renderer_begin_frame */
/* and at_renderer_end_frame.                                          */
AT_API void at_renderer3d_begin(at_renderer3d *renderer3d, const at_camera3d *camera);

/* Sorts and draws everything collected since begin. */
AT_API void at_renderer3d_end(at_renderer3d *renderer3d);

/* Counts of the current frame so far (see at_renderer3d_stats). */
AT_API at_renderer3d_stats at_renderer3d_get_stats(const at_renderer3d *renderer3d);

/* ------------------------------------------------------------------------ */
/* Meshes: only valid between at_renderer3d_begin and at_renderer3d_end     */
/* ------------------------------------------------------------------------ */

/* Draws a mesh placed in the world by model (e.g. at_transform_to_mat4) */
/* with the material (copied). NULL uses at_material_make().             */
AT_API void at_renderer3d_draw_mesh_ex(at_renderer3d *renderer3d, const at_mesh *mesh,
                                       at_mat4 model, const at_material *material);

/* Shortcut: the default material (at_material_make) in a solid color. */
AT_API void at_renderer3d_draw_mesh(at_renderer3d *renderer3d, const at_mesh *mesh,
                                    at_mat4 model, at_color color);

/* ------------------------------------------------------------------------ */
/* Lights: only valid between at_renderer3d_begin and at_renderer3d_end.    */
/* Copied; each lights every mesh of the pass (see light.h).                */
/* ------------------------------------------------------------------------ */

AT_API void at_renderer3d_add_directional_light(at_renderer3d *renderer3d,
                                                const at_directional_light *light);
AT_API void at_renderer3d_add_point_light(at_renderer3d *renderer3d, const at_point_light *light);
AT_API void at_renderer3d_add_spot_light(at_renderer3d *renderer3d, const at_spot_light *light);

/* Light that reaches every surface from all around, so the sides facing  */
/* away from the lights aren't black: sky on faces pointing up, ground on */
/* faces pointing down, blended in between. Colors are sRGB, multiplied   */
/* by intensity. Stays until changed; can be called at any time. The      */
/* default is a soft bluish sky over a dark ground.                       */
AT_API void at_renderer3d_set_ambient(at_renderer3d *renderer3d, at_color sky, at_color ground,
                                      float intensity);

AT_EXTERN_C_END

#endif /* ARCHTYPE_RENDERER_RENDERER3D_H */