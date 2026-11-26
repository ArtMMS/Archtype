#ifndef ARCHTYPE_RENDERER_RENDERER2D_H
#define ARCHTYPE_RENDERER_RENDERER2D_H

#include "archtype/core/defines.h"
#include "archtype/math/vec.h"
#include "archtype/renderer/camera2d.h"
#include "archtype/renderer/color.h"
#include "archtype/renderer/renderer.h"

AT_EXTERN_C_BEGIN

/*
 * 2D renderer: draws shapes in world space, seen through an at_camera2d.
 *
 * Shapes are not drawn one by one: they are collected in a vertex buffer
 * and sent to the GPU together (batching), in the order they were drawn.
 * Later shapes are drawn on top of earlier ones, and alpha is blended.
 *
 * Usage, every frame:
 *
 *   if (at_renderer_begin_frame(renderer)) {
 *       at_renderer2d_begin(r2d, &camera);
 *       at_renderer2d_draw_rect(r2d, at_vec2_make(0, 0), at_vec2_make(100, 50),
 *                               0.0f, at_color_rgb(1, 0, 0));
 *       at_renderer2d_end(r2d);
 *       at_renderer_end_frame(renderer);
 *   }
 *
 * begin/end can be used more than once per frame, e.g. once with the world
 * camera and once with a fixed camera for the HUD.
 *
 * at_renderer2d_end binds its own pipeline: after it, bind your pipeline
 * again before calling at_renderer_draw or at_renderer_draw_indexed.
 */

/* Opaque 2D renderer handle. */
typedef struct at_renderer2d at_renderer2d;

#define AT_RENDERER2D_DEFAULT_MAX_VERTICES 131072 /* About 21 000 rectangles per frame */

/* Zero-initialize for the defaults, or pass NULL. */
typedef struct at_renderer2d_desc {
    /* Most vertices drawn per frame (0 = AT_RENDERER2D_DEFAULT_MAX_VERTICES). */
    /* A rectangle or a line uses 6, a triangle 3, a circle 3 per segment.     */
    uint32_t max_vertices;
} at_renderer2d_desc;

/* Creates a 2D renderer. desc may be NULL. Returns NULL on failure. */
AT_API at_renderer2d *at_renderer2d_create(at_renderer *renderer, const at_renderer2d_desc *desc);

/* Destroys the 2D renderer. Passing NULL is allowed. Destroy it before the */
/* renderer it was created with. Waits for the GPU.                         */
AT_API void at_renderer2d_destroy(at_renderer2d *renderer2d);

/* Starts drawing with the given camera (NULL uses at_camera2d_make()).     */
/* Only valid between at_renderer_begin_frame and at_renderer_end_frame.    */
AT_API void at_renderer2d_begin(at_renderer2d *renderer2d, const at_camera2d *camera);

/* Sends everything drawn since begin to the GPU. */
AT_API void at_renderer2d_end(at_renderer2d *renderer2d);

/* ------------------------------------------------------------------------ */
/* Shapes: only valid between at_renderer2d_begin and at_renderer2d_end.    */
/* Positions and sizes are in world units (pixels at zoom 1, +Y up).        */
/* ------------------------------------------------------------------------ */

/* Filled triangle with corners a, b and c (any order). */
AT_API void at_renderer2d_draw_triangle(at_renderer2d *renderer2d,
                                        at_vec2 a, at_vec2 b, at_vec2 c, at_color color);

/* Filled rectangle centered at center, turned by rotation radians */
/* (counter-clockwise) around its center.                          */
AT_API void at_renderer2d_draw_rect(at_renderer2d *renderer2d, at_vec2 center, at_vec2 size,
                                    float rotation, at_color color);

/* Filled circle. The number of segments grows with the size on screen, */
/* so circles stay round when zooming in.                               */
AT_API void at_renderer2d_draw_circle(at_renderer2d *renderer2d, at_vec2 center, float radius,
                                      at_color color);

/* Line from `from` to `to`, thickness world units wide, with flat ends. */
/* For a line that is always 1 pixel wide, use 1.0f / camera.zoom.        */
AT_API void at_renderer2d_draw_line(at_renderer2d *renderer2d, at_vec2 from, at_vec2 to,
                                    float thickness, at_color color);

AT_EXTERN_C_END

#endif /* ARCHTYPE_RENDERER_RENDERER2D_H */