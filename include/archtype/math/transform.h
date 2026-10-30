#ifndef ARCHTYPE_MATH_TRANSFORM_H
#define ARCHTYPE_MATH_TRANSFORM_H

#include "archtype/math/quat.h"

/* Position, rotation and scale of an object. */
typedef struct at_transform {
    at_vec3 position;
    at_quat rotation;
    at_vec3 scale;
} at_transform;

/* Position at origin, no rotation, scale 1. */
static inline at_transform at_transform_identity(void) {
    at_transform t;
    t.position = at_vec3_zero();
    t.rotation = at_quat_identity();
    t.scale    = at_vec3_one();
    return t;
}

/* Model matrix: scale first, then rotation, then translation (T * R * S). */
static inline at_mat4 at_transform_to_mat4(at_transform t) {
    at_mat4 r = at_quat_to_mat4(t.rotation);

    for (int row = 0; row < 3; ++row) {
        r.m[0][row] *= t.scale.x;
        r.m[1][row] *= t.scale.y;
        r.m[2][row] *= t.scale.z;
    }

    r.m[3][0] = t.position.x;
    r.m[3][1] = t.position.y;
    r.m[3][2] = t.position.z;
    return r;
}

/* Local axes of the object in world space (unit length). */
static inline at_vec3 at_transform_forward(at_transform t) {
    return at_quat_rotate_vec3(t.rotation, at_vec3_forward());
}

static inline at_vec3 at_transform_right(at_transform t) {
    return at_quat_rotate_vec3(t.rotation, at_vec3_right());
}

static inline at_vec3 at_transform_up(at_transform t) {
    return at_quat_rotate_vec3(t.rotation, at_vec3_up());
}

#endif /* ARCHTYPE_MATH_TRANSFORM_H */