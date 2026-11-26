#ifndef ARCHTYPE_RENDERER_CAMERA2D_H
#define ARCHTYPE_RENDERER_CAMERA2D_H

#include "archtype/core/defines.h"
#include "archtype/math/mat4.h"

AT_EXTERN_C_BEGIN

/*
 * Orthographic camera for 2D scenes.
 *
 * World space follows the rest of the engine: +X right, +Y up. One world
 * unit is one pixel at zoom 1, so sprites keep their size in pixels.
 *
 * Screen space is the window in pixels: (0, 0) is the top-left corner and
 * +Y points down, the same units as at_window_get_mouse_position and
 * at_renderer_get_size.
 *
 * The viewport size passed to the functions below is the size in pixels of
 * the area being drawn, usually at_renderer_get_size.
 */
typedef struct at_camera2d {
    at_vec2 position; /* World point shown at the center of the screen      */
    float   zoom;     /* 1 = one unit per pixel, 2 = everything twice as big */
    float   rotation; /* Radians, counter-clockwise                         */
} at_camera2d;

/* Camera at the origin, zoom 1, no rotation. */
AT_API at_camera2d at_camera2d_make(void);

/* Matrix that takes world positions to Vulkan clip space (projection * view). */
/* Upload it to the shaders, e.g. in a uniform buffer.                         */
AT_API at_mat4 at_camera2d_view_projection(const at_camera2d *camera,
                                           float viewport_width, float viewport_height);

/* Converts a screen position (pixels, Y down) to world space, e.g. to find */
/* what is under the mouse.                                                 */
AT_API at_vec2 at_camera2d_screen_to_world(const at_camera2d *camera, at_vec2 screen,
                                           float viewport_width, float viewport_height);

/* Converts a world position to screen space (pixels, Y down). */
AT_API at_vec2 at_camera2d_world_to_screen(const at_camera2d *camera, at_vec2 world,
                                           float viewport_width, float viewport_height);

/* Multiplies the zoom by factor (> 1 zooms in, < 1 zooms out) while keeping */
/* the world point under the screen position in place, like zooming toward  */
/* the mouse cursor in a map.                                                */
AT_API void at_camera2d_zoom_at(at_camera2d *camera, at_vec2 screen, float factor,
                                float viewport_width, float viewport_height);

AT_EXTERN_C_END

#endif /* ARCHTYPE_RENDERER_CAMERA2D_H */