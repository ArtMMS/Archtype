#ifndef ARCHTYPE_RENDERER_RENDERER2D_H
#define ARCHTYPE_RENDERER_RENDERER2D_H

#include "archtype/core/defines.h"
#include "archtype/math/vec.h"
#include "archtype/renderer/camera2d.h"
#include "archtype/renderer/color.h"
#include "archtype/renderer/pipeline.h"
#include "archtype/renderer/renderer.h"
#include "archtype/renderer/texture.h"

AT_EXTERN_C_BEGIN

/*
 * 2D renderer: draws shapes and sprites in world space, seen through an
 * at_camera2d.
 *
 * Nothing is drawn right away: shapes and sprites are collected between
 * begin and end, and at_renderer2d_end sends them to the GPU together.
 *
 * Draw order (what ends up on top):
 *   - Higher layers are drawn over lower ones (at_renderer2d_set_layer).
 *   - In the same layer, later draws go over earlier ones.
 *   - So you can draw in any order: e.g. each character's shadow (layer 0)
 *     and then the character (layer 1), one after another, and all the
 *     shadows still end up below all the characters.
 *
 * Batching: after sorting, everything in a row that uses the same texture
 * and blend mode is drawn with one draw call. Shapes all share one texture,
 * so they batch with each other.
 *
 * Transparency: the alpha of colors and textures is blended by default
 * (AT_BLEND_ALPHA). at_renderer2d_set_blend_mode also offers additive
 * blending (glows, light, particles) and opaque drawing.
 *
 * Usage, every frame:
 *
 *   if (at_renderer_begin_frame(renderer)) {
 *       at_renderer2d_begin(r2d, &camera);
 *       at_renderer2d_draw_rect(r2d, at_vec2_make(0, 0), at_vec2_make(100, 50),
 *                               0.0f, at_color_rgb(1, 0, 0));
 *       at_renderer2d_end(r2d);
 *       at_renderer_end_frame(renderer);
 *   }
 *
 * begin/end can be used more than once per frame, e.g. once with the world
 * camera and once with a fixed camera for the HUD. Layers only sort inside
 * one begin/end: everything in a later begin/end is drawn on top.
 *
 * at_renderer2d_end binds its own pipeline: after it, bind your pipeline
 * again before calling at_renderer_draw or at_renderer_draw_indexed.
 *
 * Textures used by sprites must stay alive until at_renderer2d_end.
 */

/* Opaque 2D renderer handle. */
typedef struct at_renderer2d at_renderer2d;

#define AT_RENDERER2D_DEFAULT_MAX_VERTICES 131072 /* About 21 000 sprites per frame */

/* Zero-initialize for the defaults, or pass NULL. */
typedef struct at_renderer2d_desc {
    /* Most vertices drawn per frame (0 = AT_RENDERER2D_DEFAULT_MAX_VERTICES). */
    /* A sprite, rectangle or line uses 6, a triangle 3, a circle 3 per segment. */
    /* Memory: about 70 bytes per vertex (GPU buffers and the CPU copy).       */
    uint32_t max_vertices;
} at_renderer2d_desc;

/* Creates a 2D renderer. desc may be NULL. Returns NULL on failure. */
AT_API at_renderer2d *at_renderer2d_create(at_renderer *renderer, const at_renderer2d_desc *desc);

/* Destroys the 2D renderer. Passing NULL is allowed. Destroy it before the */
/* renderer it was created with. Waits for the GPU.                         */
AT_API void at_renderer2d_destroy(at_renderer2d *renderer2d);

/* Starts drawing with the given camera (NULL uses at_camera2d_make()).     */
/* Only valid between at_renderer_begin_frame and at_renderer_end_frame.    */
AT_API void at_renderer2d_begin(at_renderer2d *renderer2d, const at_camera2d *camera);

/* Sorts everything drawn since begin by layer and sends it to the GPU. */
AT_API void at_renderer2d_end(at_renderer2d *renderer2d);

/* ------------------------------------------------------------------------ */
/* Draw state: applies to the next shapes and sprites, until changed.       */
/* at_renderer2d_begin resets it to layer 0 and AT_BLEND_ALPHA.             */
/* ------------------------------------------------------------------------ */

/* Layer of the next draws. Higher layers are drawn on top. Any int32_t,  */
/* negative too: e.g. -1 background, 0 world, 10 effects. For top-down    */
/* games, a layer from the Y position (-(int32_t)y) puts things lower on  */
/* the screen in front.                                                   */
AT_API void at_renderer2d_set_layer(at_renderer2d *renderer2d, int32_t layer);

AT_API int32_t at_renderer2d_get_layer(const at_renderer2d *renderer2d);

/* How the next draws are combined with what is under them:               */
/*   AT_BLEND_ALPHA     transparency from the alpha (default)             */
/*   AT_BLEND_ADDITIVE  colors are added: glows, fire, light, particles   */
/*   AT_BLEND_NONE      opaque: alpha is ignored                          */
AT_API void at_renderer2d_set_blend_mode(at_renderer2d *renderer2d, at_blend_mode mode);

AT_API at_blend_mode at_renderer2d_get_blend_mode(const at_renderer2d *renderer2d);

/* ------------------------------------------------------------------------ */
/* Sprites                                                                   */
/* ------------------------------------------------------------------------ */

/* Everything about one sprite. Start from at_sprite_desc_make, which fills */
/* the defaults, and change what you need.                                  */
typedef struct at_sprite_desc {
    at_texture *texture;  /* NULL draws a solid rectangle in the tint color   */
    at_vec2     position; /* World position of the origin point               */
    at_vec2     size;     /* World units; (0, 0) uses the texture size, so    */
                          /* the sprite is 1 texel per pixel at zoom 1        */
    at_vec2     origin;   /* Point of the sprite placed at position, and the  */
                          /* point it turns around, from 0 to 1 on each side: */
                          /* (0, 0) bottom-left, (0.5, 0.5) center (default), */
                          /* (0.5, 0) bottom center, e.g. a character's feet  */
    float       rotation; /* Radians, counter-clockwise                       */
    at_color    tint;     /* Multiplies the texture colors (white keeps them) */
                          /* and its alpha fades the sprite                   */
    bool        flip_x;   /* Mirror left-right, e.g. a character facing left  */
    bool        flip_y;   /* Mirror top-bottom                                */
} at_sprite_desc;

/* Sprite of the whole texture at position: texture size, centered origin, */
/* no rotation, white tint, not flipped.                                   */
static inline at_sprite_desc at_sprite_desc_make(at_texture *texture, at_vec2 position) {
    at_sprite_desc sprite;
    sprite.texture  = texture;
    sprite.position = position;
    sprite.size     = at_vec2_zero();
    sprite.origin   = at_vec2_make(0.5f, 0.5f);
    sprite.rotation = 0.0f;
    sprite.tint     = at_color_white();
    sprite.flip_x   = false;
    sprite.flip_y   = false;
    return sprite;
}

/* ------------------------------------------------------------------------ */
/* Drawing: only valid between at_renderer2d_begin and at_renderer2d_end.   */
/* Positions and sizes are in world units (pixels at zoom 1, +Y up).        */
/* ------------------------------------------------------------------------ */

/* Sprite centered at center, turned by rotation radians around its center. */
/* size (0, 0) uses the texture size. tint multiplies the texture colors    */
/* (at_color_white() keeps them). texture NULL draws a solid rectangle.     */
AT_API void at_renderer2d_draw_sprite(at_renderer2d *renderer2d, at_texture *texture,
                                      at_vec2 center, at_vec2 size, float rotation,
                                      at_color tint);

/* Sprite with every option: origin, flipping... (see at_sprite_desc). */
AT_API void at_renderer2d_draw_sprite_ex(at_renderer2d *renderer2d, const at_sprite_desc *sprite);

/* Filled triangle with corners a, b and c (any order). */
AT_API void at_renderer2d_draw_triangle(at_renderer2d *renderer2d,
                                        at_vec2 a, at_vec2 b, at_vec2 c, at_color color);

/* Filled rectangle centered at center, turned by rotation radians */
/* (counter-clockwise) around its center.                          */
AT_API void at_renderer2d_draw_rect(at_renderer2d *renderer2d, at_vec2 center, at_vec2 size,
                                    float rotation, at_color color);

/* Filled circle. The number of segments grows with the size on screen, */
/* so circles stay round when zooming in.                               */
AT_API void at_renderer2d_draw_circle(at_renderer2d *renderer2d, at_vec2 center, float radius,
                                      at_color color);

/* Line from `from` to `to`, thickness world units wide, with flat ends. */
/* For a line that is always 1 pixel wide, use 1.0f / camera.zoom.        */
AT_API void at_renderer2d_draw_line(at_renderer2d *renderer2d, at_vec2 from, at_vec2 to,
                                    float thickness, at_color color);

AT_EXTERN_C_END

#endif /* ARCHTYPE_RENDERER_RENDERER2D_H */