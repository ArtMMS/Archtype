#ifndef ARCHTYPE_RENDERER_LIGHT_H
#define ARCHTYPE_RENDERER_LIGHT_H

#include "archtype/core/defines.h"
#include "archtype/math/scalar.h"
#include "archtype/math/vec.h"
#include "archtype/renderer/color.h"

AT_EXTERN_C_BEGIN

/*
 * Lights for the 3D renderer (at_renderer3d_add_*_light).
 *
 * Every light has a color (sRGB, like every color of the engine) and an
 * intensity that multiplies it. Surfaces are shaded with Blinn-Phong:
 * diffuse light, brightest on faces turned toward the light, plus a
 * highlight set by the material's specular and shininess.
 *
 * Point and spot lights are as bright as their intensity at the light
 * itself and fade with the square of the distance (intensity /
 * (distance^2 + 1)), reaching zero smoothly at their range: nothing past
 * the range is lit, which also keeps far lights cheap.
 */

/* Light from far away, the same everywhere: the sun or the moon. */
typedef struct at_directional_light {
    at_vec3  direction; /* Where the light travels, e.g. (0, -1, 0) straight down */
    at_color color;
    float    intensity; /* 1 = a full sun                                          */
} at_directional_light;

/* Light from a point, in every direction: a lamp, a torch, a spark. */
typedef struct at_point_light {
    at_vec3  position;
    at_color color;
    float    intensity; /* Brightness at the light (see above)     */
    float    range;     /* World units: nothing past it is lit     */
} at_point_light;

/* Light from a point, in a cone: a flashlight, a stage light. */
typedef struct at_spot_light {
    at_vec3  position;
    at_vec3  direction;   /* Where the cone points                            */
    at_color color;
    float    intensity;   /* Brightness at the light (see above)              */
    float    range;       /* World units: nothing past it is lit              */
    float    inner_angle; /* Radians from the center: full brightness inside  */
    float    outer_angle; /* Radians from the center: no light outside; in    */
                          /* between, the light fades out                     */
} at_spot_light;

static inline at_directional_light at_directional_light_make(at_vec3 direction, at_color color,
                                                             float intensity) {
    at_directional_light light;
    light.direction = direction;
    light.color     = color;
    light.intensity = intensity;
    return light;
}

static inline at_point_light at_point_light_make(at_vec3 position, at_color color,
                                                 float intensity, float range) {
    at_point_light light;
    light.position  = position;
    light.color     = color;
    light.intensity = intensity;
    light.range     = range;
    return light;
}

/* A cone 40 degrees wide (20 from the center), fading out to 60 degrees */
/* (30 from the center).                                                 */
static inline at_spot_light at_spot_light_make(at_vec3 position, at_vec3 direction,
                                               at_color color, float intensity, float range) {
    at_spot_light light;
    light.position    = position;
    light.direction   = direction;
    light.color       = color;
    light.intensity   = intensity;
    light.range       = range;
    light.inner_angle = at_radians(20.0f);
    light.outer_angle = at_radians(30.0f);
    return light;
}

AT_EXTERN_C_END

#endif /* ARCHTYPE_RENDERER_LIGHT_H */