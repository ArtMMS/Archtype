#ifndef ARCHTYPE_RENDERER_MATERIAL_H
#define ARCHTYPE_RENDERER_MATERIAL_H

#include "archtype/core/defines.h"
#include "archtype/math/vec.h"
#include "archtype/renderer/color.h"
#include "archtype/renderer/texture.h"

AT_EXTERN_C_BEGIN

/*
 * Material: how the surface of a mesh looks, for the 3D renderer.
 *
 * A plain struct: start from at_material_make, change what you need and
 * pass it to at_renderer3d_draw_mesh_ex. It is copied when drawn, so the
 * same variable can be changed and drawn again for the next object.
 *
 * The surface color is base_color times base_texture. Light makes it
 * brighter or darker and adds highlights (specular), unless the material
 * is unlit. emissive is added on top: light the surface gives off itself,
 * so it shows even in the dark (it doesn't light other objects).
 */
typedef struct at_material {
    at_color    base_color;        /* sRGB. Its alpha (times the texture's) is the */
                                   /* opacity when transparent, and the value      */
                                   /* tested by alpha_cutoff                       */
    at_texture *base_texture;      /* Multiplied by base_color; NULL = none. Must  */
                                   /* stay alive until at_renderer3d_end           */
    at_vec2     uv_scale;          /* Texture coordinates become uv * scale +      */
    at_vec2     uv_offset;         /* offset: scale (4, 4) repeats the texture 4   */
                                   /* times on each side                           */
    float       specular;          /* Strength of the highlights, 0 = matte        */
    float       shininess;         /* Highlight size: 4 is wide and soft, 256 is   */
                                   /* small and sharp (Blinn-Phong exponent, >= 1) */
    at_color    emissive;          /* sRGB light given off; black = none           */
                                   /* (alpha unused)                               */
    float       emissive_strength; /* Multiplies emissive: above 1 for bright glows */
    float       alpha_cutoff;      /* Pixels with less alpha are not drawn at all: */
                                   /* holes in fences and leaves. 0 = off          */
    bool        transparent;       /* Blend with what is behind, using the alpha   */
    bool        double_sided;      /* Draw the back faces too (planes, leaves)     */
    bool        unlit;             /* Ignore the lights: shown with its own colors */
} at_material;

/* White, no texture, a soft highlight (specular 0.3, shininess 32), no */
/* emission, opaque, one-sided and lit.                                 */
static inline at_material at_material_make(void) {
    at_material material;
    material.base_color        = at_color_white();
    material.base_texture      = NULL;
    material.uv_scale          = at_vec2_make(1.0f, 1.0f);
    material.uv_offset         = at_vec2_zero();
    material.specular          = 0.3f;
    material.shininess         = 32.0f;
    material.emissive          = at_color_black();
    material.emissive_strength = 1.0f;
    material.alpha_cutoff      = 0.0f;
    material.transparent       = false;
    material.double_sided      = false;
    material.unlit             = false;
    return material;
}

AT_EXTERN_C_END

#endif /* ARCHTYPE_RENDERER_MATERIAL_H */