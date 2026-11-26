#ifndef ARCHTYPE_RENDERER_COLOR_H
#define ARCHTYPE_RENDERER_COLOR_H

#include "archtype/core/defines.h"

AT_EXTERN_C_BEGIN

/*
 * RGBA color. Components go from 0 to 1 and are sRGB, like the values of a
 * color picker or an image editor (the same as at_renderer_set_clear_color).
 * Alpha is opacity: 0 is invisible, 1 is opaque.
 */
typedef struct at_color {
    float r, g, b, a;
} at_color;

static inline at_color at_color_rgba(float r, float g, float b, float a) {
    at_color color;
    color.r = r;
    color.g = g;
    color.b = b;
    color.a = a;
    return color;
}

/* Opaque color. */
static inline at_color at_color_rgb(float r, float g, float b) {
    return at_color_rgba(r, g, b, 1.0f);
}

/* Color from a hex value in 0xRRGGBBAA order, e.g. 0xFF8800FF is opaque orange. */
static inline at_color at_color_hex(uint32_t rgba) {
    return at_color_rgba((float)((rgba >> 24) & 0xFFu) / 255.0f,
                         (float)((rgba >> 16) & 0xFFu) / 255.0f,
                         (float)((rgba >>  8) & 0xFFu) / 255.0f,
                         (float)( rgba        & 0xFFu) / 255.0f);
}

/* The same color with another alpha. */
static inline at_color at_color_with_alpha(at_color color, float alpha) {
    color.a = alpha;
    return color;
}

static inline at_color at_color_white(void) { return at_color_rgb(1.0f, 1.0f, 1.0f); }
static inline at_color at_color_black(void) { return at_color_rgb(0.0f, 0.0f, 0.0f); }

AT_EXTERN_C_END

#endif /* ARCHTYPE_RENDERER_COLOR_H */