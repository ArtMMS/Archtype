#ifndef ARCHTYPE_MATH_VEC_H
#define ARCHTYPE_MATH_VEC_H

#include "archtype/math/scalar.h"

typedef struct at_vec2 {
    float x, y;
} at_vec2;

typedef struct at_vec3 {
    float x, y, z;
} at_vec3;

typedef struct at_vec4 {
    float x, y, z, w;
} at_vec4;

/* ======================================================================== */
/* at_vec2                                                                  */
/* ======================================================================== */

static inline at_vec2 at_vec2_make(float x, float y) {
    at_vec2 r = { x, y };
    return r;
}

static inline at_vec2 at_vec2_zero(void) {
    return at_vec2_make(0.0f, 0.0f);
}

static inline at_vec2 at_vec2_add(at_vec2 a, at_vec2 b) {
    return at_vec2_make(a.x + b.x, a.y + b.y);
}

static inline at_vec2 at_vec2_sub(at_vec2 a, at_vec2 b) {
    return at_vec2_make(a.x - b.x, a.y - b.y);
}

/* Component-wise multiplication. */
static inline at_vec2 at_vec2_mul(at_vec2 a, at_vec2 b) {
    return at_vec2_make(a.x * b.x, a.y * b.y);
}

static inline at_vec2 at_vec2_scale(at_vec2 v, float s) {
    return at_vec2_make(v.x * s, v.y * s);
}

static inline at_vec2 at_vec2_negate(at_vec2 v) {
    return at_vec2_make(-v.x, -v.y);
}

static inline float at_vec2_dot(at_vec2 a, at_vec2 b) {
    return a.x * b.x + a.y * b.y;
}

static inline float at_vec2_length_sq(at_vec2 v) {
    return at_vec2_dot(v, v);
}

static inline float at_vec2_length(at_vec2 v) {
    return sqrtf(at_vec2_length_sq(v));
}

/* Returns a unit-length vector, or zero if the input is (nearly) zero. */
static inline at_vec2 at_vec2_normalize(at_vec2 v) {
    float length = at_vec2_length(v);
    if (length <= AT_EPSILON) {
        return at_vec2_zero();
    }
    return at_vec2_scale(v, 1.0f / length);
}

static inline float at_vec2_distance(at_vec2 a, at_vec2 b) {
    return at_vec2_length(at_vec2_sub(a, b));
}

static inline at_vec2 at_vec2_lerp(at_vec2 a, at_vec2 b, float t) {
    return at_vec2_make(at_lerpf(a.x, b.x, t), at_lerpf(a.y, b.y, t));
}

/* ======================================================================== */
/* at_vec3                                                                  */
/* ======================================================================== */

static inline at_vec3 at_vec3_make(float x, float y, float z) {
    at_vec3 r = { x, y, z };
    return r;
}

static inline at_vec3 at_vec3_zero(void) {
    return at_vec3_make(0.0f, 0.0f, 0.0f);
}

static inline at_vec3 at_vec3_one(void) {
    return at_vec3_make(1.0f, 1.0f, 1.0f);
}

/* World axes (right-handed, Y up, camera looks down -Z). */
static inline at_vec3 at_vec3_right(void)   { return at_vec3_make(1.0f, 0.0f,  0.0f); }
static inline at_vec3 at_vec3_up(void)      { return at_vec3_make(0.0f, 1.0f,  0.0f); }
static inline at_vec3 at_vec3_forward(void) { return at_vec3_make(0.0f, 0.0f, -1.0f); }

static inline at_vec3 at_vec3_add(at_vec3 a, at_vec3 b) {
    return at_vec3_make(a.x + b.x, a.y + b.y, a.z + b.z);
}

static inline at_vec3 at_vec3_sub(at_vec3 a, at_vec3 b) {
    return at_vec3_make(a.x - b.x, a.y - b.y, a.z - b.z);
}

/* Component-wise multiplication. */
static inline at_vec3 at_vec3_mul(at_vec3 a, at_vec3 b) {
    return at_vec3_make(a.x * b.x, a.y * b.y, a.z * b.z);
}

static inline at_vec3 at_vec3_scale(at_vec3 v, float s) {
    return at_vec3_make(v.x * s, v.y * s, v.z * s);
}

static inline at_vec3 at_vec3_negate(at_vec3 v) {
    return at_vec3_make(-v.x, -v.y, -v.z);
}

static inline float at_vec3_dot(at_vec3 a, at_vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline at_vec3 at_vec3_cross(at_vec3 a, at_vec3 b) {
    return at_vec3_make(a.y * b.z - a.z * b.y,
                        a.z * b.x - a.x * b.z,
                        a.x * b.y - a.y * b.x);
}

static inline float at_vec3_length_sq(at_vec3 v) {
    return at_vec3_dot(v, v);
}

static inline float at_vec3_length(at_vec3 v) {
    return sqrtf(at_vec3_length_sq(v));
}

/* Returns a unit-length vector, or zero if the input is (nearly) zero. */
static inline at_vec3 at_vec3_normalize(at_vec3 v) {
    float length = at_vec3_length(v);
    if (length <= AT_EPSILON) {
        return at_vec3_zero();
    }
    return at_vec3_scale(v, 1.0f / length);
}

static inline float at_vec3_distance(at_vec3 a, at_vec3 b) {
    return at_vec3_length(at_vec3_sub(a, b));
}

static inline at_vec3 at_vec3_lerp(at_vec3 a, at_vec3 b, float t) {
    return at_vec3_make(at_lerpf(a.x, b.x, t),
                        at_lerpf(a.y, b.y, t),
                        at_lerpf(a.z, b.z, t));
}

/* ======================================================================== */
/* at_vec4                                                                  */
/* ======================================================================== */

static inline at_vec4 at_vec4_make(float x, float y, float z, float w) {
    at_vec4 r = { x, y, z, w };
    return r;
}

static inline at_vec4 at_vec4_zero(void) {
    return at_vec4_make(0.0f, 0.0f, 0.0f, 0.0f);
}

/* Builds a vec4 from a vec3. Use w = 1 for points and w = 0 for directions. */
static inline at_vec4 at_vec4_from_vec3(at_vec3 v, float w) {
    return at_vec4_make(v.x, v.y, v.z, w);
}

static inline at_vec3 at_vec4_to_vec3(at_vec4 v) {
    return at_vec3_make(v.x, v.y, v.z);
}

static inline at_vec4 at_vec4_add(at_vec4 a, at_vec4 b) {
    return at_vec4_make(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
}

static inline at_vec4 at_vec4_sub(at_vec4 a, at_vec4 b) {
    return at_vec4_make(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w);
}

/* Component-wise multiplication. */
static inline at_vec4 at_vec4_mul(at_vec4 a, at_vec4 b) {
    return at_vec4_make(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w);
}

static inline at_vec4 at_vec4_scale(at_vec4 v, float s) {
    return at_vec4_make(v.x * s, v.y * s, v.z * s, v.w * s);
}

static inline float at_vec4_dot(at_vec4 a, at_vec4 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

static inline float at_vec4_length(at_vec4 v) {
    return sqrtf(at_vec4_dot(v, v));
}

static inline at_vec4 at_vec4_lerp(at_vec4 a, at_vec4 b, float t) {
    return at_vec4_make(at_lerpf(a.x, b.x, t),
                        at_lerpf(a.y, b.y, t),
                        at_lerpf(a.z, b.z, t),
                        at_lerpf(a.w, b.w, t));
}

#endif /* ARCHTYPE_MATH_VEC_H */