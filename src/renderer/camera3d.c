#include "archtype/renderer/camera3d.h"

/* Smallest values allowed: keep the math away from dividing by zero */
#define AT_CAMERA3D_MIN_FOV    0.01f
#define AT_CAMERA3D_MAX_FOV    3.13f /* Just under 180 degrees */
#define AT_CAMERA3D_MIN_NEAR   1e-5f
#define AT_CAMERA3D_MIN_ASPECT 1e-4f

/* Unit quaternion of the rotation whose matrix has the columns x, y, z */
/* (an orthonormal, right-handed basis).                                */
static at_quat quat_from_basis(at_vec3 x, at_vec3 y, at_vec3 z) {
    /* r[row][column]: x, y and z are the columns */
    const float r00 = x.x, r01 = y.x, r02 = z.x;
    const float r10 = x.y, r11 = y.y, r12 = z.y;
    const float r20 = x.z, r21 = y.z, r22 = z.z;

    float   trace = r00 + r11 + r22;
    at_quat q;

    if (trace > 0.0f) {
        float s = 0.5f / sqrtf(trace + 1.0f);
        q.w = 0.25f / s;
        q.x = (r21 - r12) * s;
        q.y = (r02 - r20) * s;
        q.z = (r10 - r01) * s;
    } else if (r00 > r11 && r00 > r22) {
        float s = 2.0f * sqrtf(1.0f + r00 - r11 - r22);
        q.w = (r21 - r12) / s;
        q.x = 0.25f * s;
        q.y = (r01 + r10) / s;
        q.z = (r02 + r20) / s;
    } else if (r11 > r22) {
        float s = 2.0f * sqrtf(1.0f + r11 - r00 - r22);
        q.w = (r02 - r20) / s;
        q.x = (r01 + r10) / s;
        q.y = 0.25f * s;
        q.z = (r12 + r21) / s;
    } else {
        float s = 2.0f * sqrtf(1.0f + r22 - r00 - r11);
        q.w = (r10 - r01) / s;
        q.x = (r02 + r20) / s;
        q.y = (r12 + r21) / s;
        q.z = 0.25f * s;
    }

    return at_quat_normalize(q);
}

at_camera3d at_camera3d_make(void) {
    at_camera3d camera;
    camera.position = at_vec3_zero();
    camera.rotation = at_quat_identity();
    camera.fov_y    = at_radians(60.0f);
    camera.near_z   = 0.1f;
    camera.far_z    = 1000.0f;
    return camera;
}

void at_camera3d_look_at(at_camera3d *camera, at_vec3 target, at_vec3 up) {
    at_vec3 to_target = at_vec3_sub(target, camera->position);
    if (at_vec3_length_sq(to_target) <= AT_EPSILON * AT_EPSILON) {
        return;
    }
    at_vec3 forward = at_vec3_normalize(to_target);

    /* Looking straight along up: any other axis works as up */
    at_vec3 right = at_vec3_cross(forward, up);
    if (at_vec3_length_sq(right) <= AT_EPSILON) {
        at_vec3 other = fabsf(forward.y) < 0.99f ? at_vec3_up() : at_vec3_make(0.0f, 0.0f, -1.0f);
        right = at_vec3_cross(forward, other);
    }
    right = at_vec3_normalize(right);

    at_vec3 camera_up = at_vec3_cross(right, forward);

    /* The camera's local axes: +X right, +Y up, -Z forward */
    camera->rotation = quat_from_basis(right, camera_up, at_vec3_negate(forward));
}

at_vec3 at_camera3d_forward(const at_camera3d *camera) {
    return at_quat_rotate_vec3(camera->rotation, at_vec3_forward());
}

at_vec3 at_camera3d_right(const at_camera3d *camera) {
    return at_quat_rotate_vec3(camera->rotation, at_vec3_right());
}

at_vec3 at_camera3d_up(const at_camera3d *camera) {
    return at_quat_rotate_vec3(camera->rotation, at_vec3_up());
}

at_mat4 at_camera3d_view(const at_camera3d *camera) {
    /* Inverse of the camera's own transform: undo the translation, then */
    /* the rotation (the conjugate of a unit quaternion is its inverse)  */
    at_mat4 rotation  = at_quat_to_mat4(at_quat_conjugate(at_quat_normalize(camera->rotation)));
    at_mat4 translate = at_mat4_translate(at_vec3_negate(camera->position));
    return at_mat4_mul(rotation, translate);
}

at_mat4 at_camera3d_projection(const at_camera3d *camera, float aspect) {
    float fov    = at_clampf(camera->fov_y, AT_CAMERA3D_MIN_FOV, AT_CAMERA3D_MAX_FOV);
    float near_z = at_maxf(camera->near_z, AT_CAMERA3D_MIN_NEAR);
    float far_z  = at_maxf(camera->far_z, near_z * 1.001f);
    return at_mat4_perspective(fov, at_maxf(aspect, AT_CAMERA3D_MIN_ASPECT), near_z, far_z);
}

at_mat4 at_camera3d_view_projection(const at_camera3d *camera,
                                    float viewport_width, float viewport_height) {
    float aspect = viewport_height > 0.0f ? viewport_width / viewport_height : 1.0f;
    return at_mat4_mul(at_camera3d_projection(camera, aspect), at_camera3d_view(camera));
}

bool at_camera3d_world_to_screen(const at_camera3d *camera, at_vec3 world,
                                 float viewport_width, float viewport_height,
                                 at_vec2 *screen) {
    at_mat4 view_projection = at_camera3d_view_projection(camera, viewport_width,
                                                          viewport_height);
    at_vec4 clip = at_mat4_mul_vec4(view_projection, at_vec4_from_vec3(world, 1.0f));

    /* w is the distance in front of the camera */
    if (clip.w <= AT_EPSILON) {
        return false;
    }

    /* Vulkan's normalized coordinates already have +Y down, like the screen */
    float ndc_x = clip.x / clip.w;
    float ndc_y = clip.y / clip.w;
    if (screen) {
        screen->x = (ndc_x + 1.0f) * 0.5f * viewport_width;
        screen->y = (ndc_y + 1.0f) * 0.5f * viewport_height;
    }
    return true;
}

void at_camera3d_screen_to_ray(const at_camera3d *camera, at_vec2 screen,
                               float viewport_width, float viewport_height,
                               at_vec3 *origin, at_vec3 *direction) {
    float ndc_x = viewport_width  > 0.0f ? screen.x / viewport_width  * 2.0f - 1.0f : 0.0f;
    float ndc_y = viewport_height > 0.0f ? screen.y / viewport_height * 2.0f - 1.0f : 0.0f;

    at_mat4 inverse = at_mat4_inverse(at_camera3d_view_projection(camera, viewport_width,
                                                                  viewport_height));

    /* The screen point on the near plane (depth 0) and on the far plane (depth 1) */
    at_vec4 near_clip = at_mat4_mul_vec4(inverse, at_vec4_make(ndc_x, ndc_y, 0.0f, 1.0f));
    at_vec4 far_clip  = at_mat4_mul_vec4(inverse, at_vec4_make(ndc_x, ndc_y, 1.0f, 1.0f));

    at_vec3 near_point = at_vec3_scale(at_vec4_to_vec3(near_clip), 1.0f / near_clip.w);
    at_vec3 far_point  = at_vec3_scale(at_vec4_to_vec3(far_clip), 1.0f / far_clip.w);

    if (origin) {
        *origin = near_point;
    }
    if (direction) {
        *direction = at_vec3_normalize(at_vec3_sub(far_point, near_point));
    }
}