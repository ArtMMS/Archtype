#ifndef ARCHTYPE_MATH_MAT4_H
#define ARCHTYPE_MATH_MAT4_H

#include "archtype/math/vec.h"

/*
 * 4x4 matrix, column-major (same memory layout as GLSL's mat4).
 * Access as m[column][row].
 *
 * Conventions used by the whole engine:
 *   - Right-handed: +X right, +Y up, the camera looks down -Z.
 *   - Projections output Vulkan clip space: depth in [0, 1] and Y flipped
 *     (Vulkan's Y axis points down on screen).
 *   - Vectors are columns: transformed = matrix * vector, so in
 *     at_mat4_mul(a, b) the transform b is applied first, then a.
 */
typedef struct at_mat4 {
    float m[4][4];
} at_mat4;

/* Pointer to the 16 floats, ready to upload to the GPU. */
static inline const float *at_mat4_ptr(const at_mat4 *matrix) {
    return &matrix->m[0][0];
}

static inline at_mat4 at_mat4_zero(void) {
    at_mat4 r;
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            r.m[c][row] = 0.0f;
        }
    }
    return r;
}

static inline at_mat4 at_mat4_identity(void) {
    at_mat4 r = at_mat4_zero();
    r.m[0][0] = 1.0f;
    r.m[1][1] = 1.0f;
    r.m[2][2] = 1.0f;
    r.m[3][3] = 1.0f;
    return r;
}

/* Returns a * b (b is applied first). */
static inline at_mat4 at_mat4_mul(at_mat4 a, at_mat4 b) {
    at_mat4 r;
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            r.m[c][row] = a.m[0][row] * b.m[c][0]
                        + a.m[1][row] * b.m[c][1]
                        + a.m[2][row] * b.m[c][2]
                        + a.m[3][row] * b.m[c][3];
        }
    }
    return r;
}

static inline at_vec4 at_mat4_mul_vec4(at_mat4 m, at_vec4 v) {
    return at_vec4_make(
        m.m[0][0] * v.x + m.m[1][0] * v.y + m.m[2][0] * v.z + m.m[3][0] * v.w,
        m.m[0][1] * v.x + m.m[1][1] * v.y + m.m[2][1] * v.z + m.m[3][1] * v.w,
        m.m[0][2] * v.x + m.m[1][2] * v.y + m.m[2][2] * v.z + m.m[3][2] * v.w,
        m.m[0][3] * v.x + m.m[1][3] * v.y + m.m[2][3] * v.z + m.m[3][3] * v.w);
}

/* Transforms a point (w = 1, affected by translation). */
static inline at_vec3 at_mat4_mul_point(at_mat4 m, at_vec3 p) {
    return at_vec4_to_vec3(at_mat4_mul_vec4(m, at_vec4_from_vec3(p, 1.0f)));
}

/* Transforms a direction (w = 0, not affected by translation). */
static inline at_vec3 at_mat4_mul_direction(at_mat4 m, at_vec3 d) {
    return at_vec4_to_vec3(at_mat4_mul_vec4(m, at_vec4_from_vec3(d, 0.0f)));
}

static inline at_mat4 at_mat4_transpose(at_mat4 m) {
    at_mat4 r;
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            r.m[c][row] = m.m[row][c];
        }
    }
    return r;
}

/* Returns the inverse of m, or the identity if m cannot be inverted. */
static inline at_mat4 at_mat4_inverse(at_mat4 m) {
    const float *a = &m.m[0][0];
    float inv[16];

    inv[0]  =  a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15]
             + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4]  = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15]
             - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8]  =  a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15]
             + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14]
             - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1]  = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15]
             - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5]  =  a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15]
             + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9]  = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15]
             - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] =  a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14]
             + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2]  =  a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15]
             + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6]  = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15]
             - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] =  a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15]
             + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14]
             - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3]  = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11]
             - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7]  =  a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11]
             + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11]
             - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] =  a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10]
             + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];

    float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (fabsf(det) <= AT_EPSILON * AT_EPSILON) {
        return at_mat4_identity();
    }

    float inv_det = 1.0f / det;
    at_mat4 r;
    float *out = &r.m[0][0];
    for (int i = 0; i < 16; ++i) {
        out[i] = inv[i] * inv_det;
    }
    return r;
}

/* ------------------------------------------------------------------------ */
/* Transform matrices                                                        */
/* ------------------------------------------------------------------------ */

static inline at_mat4 at_mat4_translate(at_vec3 t) {
    at_mat4 r = at_mat4_identity();
    r.m[3][0] = t.x;
    r.m[3][1] = t.y;
    r.m[3][2] = t.z;
    return r;
}

static inline at_mat4 at_mat4_scale(at_vec3 s) {
    at_mat4 r = at_mat4_identity();
    r.m[0][0] = s.x;
    r.m[1][1] = s.y;
    r.m[2][2] = s.z;
    return r;
}

/* Rotation of `angle` radians around `axis` (counter-clockwise). */
static inline at_mat4 at_mat4_rotate(at_vec3 axis, float angle) {
    at_vec3 n = at_vec3_normalize(axis);
    float c = cosf(angle);
    float s = sinf(angle);
    float t = 1.0f - c;

    at_mat4 r = at_mat4_identity();
    r.m[0][0] = t * n.x * n.x + c;
    r.m[0][1] = t * n.x * n.y + s * n.z;
    r.m[0][2] = t * n.x * n.z - s * n.y;

    r.m[1][0] = t * n.x * n.y - s * n.z;
    r.m[1][1] = t * n.y * n.y + c;
    r.m[1][2] = t * n.y * n.z + s * n.x;

    r.m[2][0] = t * n.x * n.z + s * n.y;
    r.m[2][1] = t * n.y * n.z - s * n.x;
    r.m[2][2] = t * n.z * n.z + c;
    return r;
}

/* ------------------------------------------------------------------------ */
/* Camera matrices                                                           */
/* ------------------------------------------------------------------------ */

/* View matrix for a camera at `eye` looking at `target`. */
static inline at_mat4 at_mat4_look_at(at_vec3 eye, at_vec3 target, at_vec3 up) {
    at_vec3 f = at_vec3_normalize(at_vec3_sub(target, eye));
    at_vec3 s = at_vec3_normalize(at_vec3_cross(f, up));
    at_vec3 u = at_vec3_cross(s, f);

    at_mat4 r = at_mat4_identity();
    r.m[0][0] = s.x;
    r.m[1][0] = s.y;
    r.m[2][0] = s.z;

    r.m[0][1] = u.x;
    r.m[1][1] = u.y;
    r.m[2][1] = u.z;

    r.m[0][2] = -f.x;
    r.m[1][2] = -f.y;
    r.m[2][2] = -f.z;

    r.m[3][0] = -at_vec3_dot(s, eye);
    r.m[3][1] = -at_vec3_dot(u, eye);
    r.m[3][2] =  at_vec3_dot(f, eye);
    return r;
}

/*
 * Perspective projection for Vulkan.
 *   fov_y:  vertical field of view in radians
 *   aspect: width / height
 *   near and far must be positive, with far > near
 */
static inline at_mat4 at_mat4_perspective(float fov_y, float aspect, float near_z, float far_z) {
    float f = 1.0f / tanf(fov_y * 0.5f);

    at_mat4 r = at_mat4_zero();
    r.m[0][0] = f / aspect;
    r.m[1][1] = -f; /* Flip Y for Vulkan */
    r.m[2][2] = far_z / (near_z - far_z);
    r.m[2][3] = -1.0f;
    r.m[3][2] = -(far_z * near_z) / (far_z - near_z);
    return r;
}

/*
 * Orthographic projection for Vulkan.
 * `top` is drawn at the top of the screen and `bottom` at the bottom:
 *   at_mat4_ortho(0, width, height, 0, ...) -> pixel coordinates, Y down
 *   at_mat4_ortho(0, width, 0, height, ...) -> Y up
 */
static inline at_mat4 at_mat4_ortho(float left, float right, float bottom, float top,
                                    float near_z, float far_z) {
    at_mat4 r = at_mat4_identity();
    r.m[0][0] =  2.0f / (right - left);
    r.m[1][1] = -2.0f / (top - bottom); /* Flip Y for Vulkan */
    r.m[2][2] = -1.0f / (far_z - near_z);
    r.m[3][0] = -(right + left) / (right - left);
    r.m[3][1] =  (top + bottom) / (top - bottom);
    r.m[3][2] = -near_z / (far_z - near_z);
    return r;
}

#endif /* ARCHTYPE_MATH_MAT4_H */