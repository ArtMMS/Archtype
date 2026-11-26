#include "archtype/renderer/fly_camera.h"

#define AT_FLY_MAX_PITCH  at_radians(89.0f) /* Never straight up or down */
#define AT_FLY_WHEEL_STEP 1.25f             /* Speed change per notch    */
#define AT_FLY_MAX_DT     0.1f              /* Longer pauses don't jump  */

at_fly_camera at_fly_camera_make(void) {
    at_fly_camera fly;
    fly.yaw             = 0.0f;
    fly.pitch           = 0.0f;
    fly.speed           = 5.0f;
    fly.min_speed       = 0.1f;
    fly.max_speed       = 500.0f;
    fly.fast_multiplier = 4.0f;
    fly.sensitivity     = 0.0025f;
    fly.looking         = false;
    return fly;
}

at_quat at_fly_camera_rotation(float yaw, float pitch) {
    /* Pitch around the camera's own X axis first, then yaw around world +Y */
    at_quat yaw_rotation   = at_quat_from_axis_angle(at_vec3_up(), yaw);
    at_quat pitch_rotation = at_quat_from_axis_angle(at_vec3_right(), pitch);
    return at_quat_mul(yaw_rotation, pitch_rotation);
}

void at_fly_camera_sync(at_fly_camera *fly, const at_camera3d *camera) {
    at_vec3 forward = at_camera3d_forward(camera);

    fly->pitch = at_clampf(asinf(at_clampf(forward.y, -1.0f, 1.0f)),
                           -AT_FLY_MAX_PITCH, AT_FLY_MAX_PITCH);

    /* Looking straight up or down, the yaw can't be read from forward: keep it */
    if (forward.x * forward.x + forward.z * forward.z > AT_EPSILON) {
        fly->yaw = atan2f(-forward.x, -forward.z);
    }
}

/* +1, -1 or 0 from a pair of opposite keys */
static float axis(const at_window *window, at_key positive, at_key positive_alt,
                  at_key negative, at_key negative_alt) {
    float value = 0.0f;
    if (at_window_is_key_down(window, positive) || at_window_is_key_down(window, positive_alt)) {
        value += 1.0f;
    }
    if (at_window_is_key_down(window, negative) || at_window_is_key_down(window, negative_alt)) {
        value -= 1.0f;
    }
    return value;
}

void at_fly_camera_update(at_fly_camera *fly, at_camera3d *camera, at_window *window,
                          float dt) {
    if (!fly || !camera || !window) {
        return;
    }
    dt = at_clampf(dt, 0.0f, AT_FLY_MAX_DT);

    /* Look: capture the mouse while the right button is held */
    bool want_look = at_window_is_mouse_button_down(window, AT_MOUSE_BUTTON_RIGHT);
    if (want_look != fly->looking) {
        at_window_set_mouse_captured(window, want_look);
        fly->looking = want_look;
    } else if (fly->looking) {
        float dx = 0.0f;
        float dy = 0.0f;
        at_window_get_mouse_delta(window, &dx, &dy);

        /* Mouse right turns right (negative yaw), mouse down looks down */
        fly->yaw  -= dx * fly->sensitivity;
        fly->pitch = at_clampf(fly->pitch - dy * fly->sensitivity,
                               -AT_FLY_MAX_PITCH, AT_FLY_MAX_PITCH);
        fly->yaw   = fmodf(fly->yaw, AT_TAU);
    }

    /* Speed: the wheel multiplies it, so it works at any scale */
    float wheel = at_window_get_mouse_wheel(window);
    if (wheel != 0.0f) {
        fly->speed = at_clampf(fly->speed * powf(AT_FLY_WHEEL_STEP, wheel),
                               fly->min_speed, fly->max_speed);
    }

    camera->rotation = at_fly_camera_rotation(fly->yaw, fly->pitch);

    /* Move: forward follows the view (flying), up/down follow the world */
    float forward_amount = axis(window, AT_KEY_W, AT_KEY_UP, AT_KEY_S, AT_KEY_DOWN);
    float right_amount   = axis(window, AT_KEY_D, AT_KEY_RIGHT, AT_KEY_A, AT_KEY_LEFT);
    float up_amount      = axis(window, AT_KEY_E, AT_KEY_SPACE, AT_KEY_Q, AT_KEY_LEFT_CTRL);

    at_vec3 move = at_vec3_add(
        at_vec3_add(at_vec3_scale(at_camera3d_forward(camera), forward_amount),
                    at_vec3_scale(at_camera3d_right(camera), right_amount)),
        at_vec3_scale(at_vec3_up(), up_amount));

    /* Diagonals are not faster than straight lines */
    if (at_vec3_length_sq(move) > AT_EPSILON) {
        float speed = fly->speed;
        if (at_window_is_key_down(window, AT_KEY_LEFT_SHIFT) ||
            at_window_is_key_down(window, AT_KEY_RIGHT_SHIFT)) {
            speed *= fly->fast_multiplier;
        }
        move             = at_vec3_scale(at_vec3_normalize(move), speed * dt);
        camera->position = at_vec3_add(camera->position, move);
    }
}