#ifndef ARCHTYPE_MATH_QUAT_H
#define ARCHTYPE_MATH_QUAT_H

#include "archtype/math/mat4.h"

/*
 * Quaternion representing a rotation: (x, y, z) is the vector part and
 * w is the scalar part. Rotation functions expect unit quaternions.
 */
typedef struct at_quat {
    float x, y, z, w;
} at_quat;

static inline at_quat at_quat_make(float x, float y, float z, float w) {
    at_quat r = { x, y, z, w };
    return r;
}

/* No rotation. */
static inline at_quat at_quat_identity(void) {
    return at_quat_make(0.0f, 0.0f, 0.0f, 1.0f);
}

/* Rotation of `angle` radians around `axis` (counter-clockwise). */
static inline at_quat at_quat_from_axis_angle(at_vec3 axis, float angle) {
    at_vec3 n = at_vec3_normalize(axis);
    float half = angle * 0.5f;
    float s = sinf(half);
    return at_quat_make(n.x * s, n.y * s, n.z * s, cosf(half));
}

static inline float at_quat_dot(at_quat a, at_quat b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

static inline float at_quat_length(at_quat q) {
    return sqrtf(at_quat_dot(q, q));
}

/* Returns a unit quaternion, or the identity if the input is (nearly) zero. */
static inline at_quat at_quat_normalize(at_quat q) {
    float length = at_quat_length(q);
    if (length <= AT_EPSILON) {
        return at_quat_identity();
    }
    float inv = 1.0f / length;
    return at_quat_make(q.x * inv, q.y * inv, q.z * inv, q.w * inv);
}

static inline at_quat at_quat_conjugate(at_quat q) {
    return at_quat_make(-q.x, -q.y, -q.z, q.w);
}

/* Inverse rotation (works for non-unit quaternions too). */
static inline at_quat at_quat_inverse(at_quat q) {
    float length_sq = at_quat_dot(q, q);
    if (length_sq <= AT_EPSILON) {
        return at_quat_identity();
    }
    float inv = 1.0f / length_sq;
    return at_quat_make(-q.x * inv, -q.y * inv, -q.z * inv, q.w * inv);
}

/* Combines rotations: the result applies b first, then a. */
static inline at_quat at_quat_mul(at_quat a, at_quat b) {
    return at_quat_make(
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}

/*
 * Rotation from Euler angles in radians (x = pitch, y = yaw, z = roll).
 * Applied in this order: roll (Z), then pitch (X), then yaw (Y).
 */
static inline at_quat at_quat_from_euler(at_vec3 euler) {
    at_quat pitch = at_quat_from_axis_angle(at_vec3_make(1.0f, 0.0f, 0.0f), euler.x);
    at_quat yaw   = at_quat_from_axis_angle(at_vec3_make(0.0f, 1.0f, 0.0f), euler.y);
    at_quat roll  = at_quat_from_axis_angle(at_vec3_make(0.0f, 0.0f, 1.0f), euler.z);
    return at_quat_mul(yaw, at_quat_mul(pitch, roll));
}

/* Rotates vector v by quaternion q. */
static inline at_vec3 at_quat_rotate_vec3(at_quat q, at_vec3 v) {
    at_vec3 u = at_vec3_make(q.x, q.y, q.z);
    at_vec3 t = at_vec3_scale(at_vec3_cross(u, v), 2.0f);
    return at_vec3_add(at_vec3_add(v, at_vec3_scale(t, q.w)), at_vec3_cross(u, t));
}

/* Rotation matrix equivalent to q. */
static inline at_mat4 at_quat_to_mat4(at_quat q) {
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

    at_mat4 r = at_mat4_identity();
    r.m[0][0] = 1.0f - 2.0f * (yy + zz);
    r.m[0][1] = 2.0f * (xy + wz);
    r.m[0][2] = 2.0f * (xz - wy);

    r.m[1][0] = 2.0f * (xy - wz);
    r.m[1][1] = 1.0f - 2.0f * (xx + zz);
    r.m[1][2] = 2.0f * (yz + wx);

    r.m[2][0] = 2.0f * (xz + wy);
    r.m[2][1] = 2.0f * (yz - wx);
    r.m[2][2] = 1.0f - 2.0f * (xx + yy);
    return r;
}

/* Spherical interpolation between two rotations (shortest path). */
static inline at_quat at_quat_slerp(at_quat a, at_quat b, float t) {
    float cos_theta = at_quat_dot(a, b);

    /* Take the shortest path */
    if (cos_theta < 0.0f) {
        b = at_quat_make(-b.x, -b.y, -b.z, -b.w);
        cos_theta = -cos_theta;
    }

    /* Very close: linear interpolation avoids dividing by ~0 */
    if (cos_theta > 1.0f - AT_EPSILON) {
        return at_quat_normalize(at_quat_make(at_lerpf(a.x, b.x, t),
                                              at_lerpf(a.y, b.y, t),
                                              at_lerpf(a.z, b.z, t),
                                              at_lerpf(a.w, b.w, t)));
    }

    float theta     = acosf(cos_theta);
    float sin_theta = sinf(theta);
    float wa = sinf((1.0f - t) * theta) / sin_theta;
    float wb = sinf(t * theta) / sin_theta;

    return at_quat_make(a.x * wa + b.x * wb,
                        a.y * wa + b.y * wb,
                        a.z * wa + b.z * wb,
                        a.w * wa + b.w * wb);
}

#endif /* ARCHTYPE_MATH_QUAT_H */