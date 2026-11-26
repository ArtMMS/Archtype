#ifndef ARCHTYPE_MATH_RECT_H
#define ARCHTYPE_MATH_RECT_H

#include "archtype/math/vec.h"

/*
 * Axis-aligned rectangle: (x, y) is the corner with the smallest
 * coordinates, then width and height.
 *
 * What "smallest" means depends on the space: in an image (texels, +Y down)
 * it is the top-left corner; in the 2D world (+Y up) it is the bottom-left.
 */
typedef struct at_rect {
    float x, y;
    float width, height;
} at_rect;

static inline at_rect at_rect_make(float x, float y, float width, float height) {
    at_rect r;
    r.x      = x;
    r.y      = y;
    r.width  = width;
    r.height = height;
    return r;
}

/* True if the rectangle has no area. */
static inline bool at_rect_is_empty(at_rect r) {
    return r.width <= 0.0f || r.height <= 0.0f;
}

static inline at_vec2 at_rect_center(at_rect r) {
    return at_vec2_make(r.x + r.width * 0.5f, r.y + r.height * 0.5f);
}

/* True if point is inside, edges included. */
static inline bool at_rect_contains(at_rect r, at_vec2 point) {
    return point.x >= r.x && point.x <= r.x + r.width &&
           point.y >= r.y && point.y <= r.y + r.height;
}

/* True if the two rectangles overlap (touching edges don't count). */
static inline bool at_rect_overlaps(at_rect a, at_rect b) {
    return a.x < b.x + b.width && b.x < a.x + a.width &&
           a.y < b.y + b.height && b.y < a.y + a.height;
}

#endif /* ARCHTYPE_MATH_RECT_H */