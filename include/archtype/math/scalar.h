#ifndef ARCHTYPE_MATH_SCALAR_H
#define ARCHTYPE_MATH_SCALAR_H

#include <math.h>

#include "archtype/core/defines.h"

#define AT_PI      3.14159265358979323846f
#define AT_TAU     6.28318530717958647692f
#define AT_EPSILON 1e-6f

/* Converts degrees to radians. */
static inline float at_radians(float degrees) {
    return degrees * (AT_PI / 180.0f);
}

/* Converts radians to degrees. */
static inline float at_degrees(float radians) {
    return radians * (180.0f / AT_PI);
}

static inline float at_minf(float a, float b) {
    return a < b ? a : b;
}

static inline float at_maxf(float a, float b) {
    return a > b ? a : b;
}

static inline float at_clampf(float value, float min, float max) {
    return value < min ? min : (value > max ? max : value);
}

/* Linear interpolation: t = 0 returns a, t = 1 returns b. */
static inline float at_lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

/* Returns true if a and b differ by at most epsilon. */
static inline bool at_nearly_equalf(float a, float b, float epsilon) {
    return fabsf(a - b) <= epsilon;
}

#endif /* ARCHTYPE_MATH_SCALAR_H */