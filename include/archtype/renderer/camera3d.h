#ifndef ARCHTYPE_RENDERER_CAMERA3D_H
#define ARCHTYPE_RENDERER_CAMERA3D_H

#include "archtype/core/defines.h"
#include "archtype/math/mat4.h"
#include "archtype/math/quat.h"

AT_EXTERN_C_BEGIN

/*
 * Perspective camera for 3D scenes.
 *
 * World space follows the rest of the engine: right-handed, +X right,
 * +Y up. With no rotation the camera looks down -Z, with +Y as its up.
 *
 * Screen space is the window in pixels: (0, 0) is the top-left corner and
 * +Y points down, the same units as at_window_get_mouse_position and
 * at_renderer_get_size.
 *
 * The viewport size passed to the functions below is the size in pixels of
 * the area being drawn, usually at_renderer_get_size.
 */
typedef struct at_camera3d {
    at_vec3 position;
    at_quat rotation; /* Identity: looking down -Z, +Y up            */
    float   fov_y;    /* Vertical field of view in radians            */
    float   near_z;   /* Closest visible distance (must be > 0)       */
    float   far_z;    /* Farthest visible distance (must be > near_z) */
} at_camera3d;

/* Camera at the origin looking down -Z, 60 degree field of view, */
/* visible from 0.1 to 1000 units.                               */
AT_API at_camera3d at_camera3d_make(void);

/* Turns the camera, without moving it, to look at target. up is the */
/* world direction that should point up on screen, usually +Y.       */
/* Nothing changes if target is at the camera position.              */
AT_API void at_camera3d_look_at(at_camera3d *camera, at_vec3 target, at_vec3 up);

/* Directions the camera is facing, in world space (unit length). */
AT_API at_vec3 at_camera3d_forward(const at_camera3d *camera);
AT_API at_vec3 at_camera3d_right(const at_camera3d *camera);
AT_API at_vec3 at_camera3d_up(const at_camera3d *camera);

/* View matrix: moves and turns the world the opposite way of the camera. */
AT_API at_mat4 at_camera3d_view(const at_camera3d *camera);

/* Projection matrix for an aspect ratio (width / height), in Vulkan clip */
/* space: depth 0 at near_z and 1 at far_z.                               */
AT_API at_mat4 at_camera3d_projection(const at_camera3d *camera, float aspect);

/* Matrix that takes world positions to Vulkan clip space (projection * view). */
/* Upload it to the shaders, e.g. in a uniform buffer.                         */
AT_API at_mat4 at_camera3d_view_projection(const at_camera3d *camera,
                                           float viewport_width, float viewport_height);

/* Finds where a world point appears on screen (pixels, Y down). Returns */
/* false, leaving *screen unchanged, if the point is behind the camera.  */
AT_API bool at_camera3d_world_to_screen(const at_camera3d *camera, at_vec3 world,
                                        float viewport_width, float viewport_height,
                                        at_vec2 *screen);

/* Ray from the camera through a screen position (pixels, Y down), e.g. to */
/* find what is under the mouse. origin is on the near plane; direction is */
/* unit length. Either pointer may be NULL.                                */
AT_API void at_camera3d_screen_to_ray(const at_camera3d *camera, at_vec2 screen,
                                      float viewport_width, float viewport_height,
                                      at_vec3 *origin, at_vec3 *direction);

AT_EXTERN_C_END

#endif /* ARCHTYPE_RENDERER_CAMERA3D_H */