#ifndef ARCHTYPE_RENDERER_FLY_CAMERA_H
#define ARCHTYPE_RENDERER_FLY_CAMERA_H

#include "archtype/core/defines.h"
#include "archtype/platform/window.h"
#include "archtype/renderer/camera3d.h"

AT_EXTERN_C_BEGIN

/*
 * Free camera for debugging 3D scenes, flying like in an editor:
 *
 *   Right mouse button (hold): look around (the mouse is captured)
 *   W A S D:                   move forward, left, back, right
 *   E or Space / Q or Ctrl:    move up / down
 *   Shift (hold):              move faster
 *   Mouse wheel:               change the speed
 *
 * The controller only keeps the angles and settings; the position and
 * rotation live in the at_camera3d it drives, so the same camera can be
 * moved by code too (call at_fly_camera_sync afterwards).
 */
typedef struct at_fly_camera {
    float yaw;             /* Radians around +Y: 0 looks down -Z, positive turns left */
    float pitch;           /* Radians: positive looks up, kept within +-89 degrees    */
    float speed;           /* Units per second                                        */
    float min_speed;       /* Limits for the mouse wheel                              */
    float max_speed;
    float fast_multiplier; /* Speed multiplier while Shift is held                    */
    float sensitivity;     /* Radians per pixel of mouse movement                     */
    bool  looking;         /* True while the right button is held and the mouse is    */
                           /* captured (managed by at_fly_camera_update)              */
} at_fly_camera;

/* Speed 5 (from 0.1 to 500), 4 times faster with Shift. */
AT_API at_fly_camera at_fly_camera_make(void);

/* Takes the yaw and pitch from where the camera is facing. Call it after */
/* moving the camera by code, e.g. after at_camera3d_look_at.             */
AT_API void at_fly_camera_sync(at_fly_camera *fly, const at_camera3d *camera);

/* Reads the window's input, then moves and turns the camera. dt is the */
/* time since the last frame, in seconds. Call it once per frame, after */
/* at_platform_poll_events.                                             */
AT_API void at_fly_camera_update(at_fly_camera *fly, at_camera3d *camera, at_window *window,
                                 float dt);

/* Rotation for a yaw and a pitch, in radians: what the fly camera gives */
/* its camera.                                                           */
AT_API at_quat at_fly_camera_rotation(float yaw, float pitch);

AT_EXTERN_C_END

#endif /* ARCHTYPE_RENDERER_FLY_CAMERA_H */