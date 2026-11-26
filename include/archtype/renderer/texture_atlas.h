#ifndef ARCHTYPE_RENDERER_TEXTURE_ATLAS_H
#define ARCHTYPE_RENDERER_TEXTURE_ATLAS_H

#include "archtype/core/defines.h"
#include "archtype/math/rect.h"
#include "archtype/renderer/renderer.h"
#include "archtype/renderer/renderer2d.h"
#include "archtype/renderer/texture.h"

AT_EXTERN_C_BEGIN

/*
 * Texture atlas: many images of any size packed into one texture.
 *
 * Sprites from different images of the same atlas share one texture, so
 * the 2D renderer can draw them all in one batch.
 *
 * Usage:
 *   1. at_texture_atlas_create: an empty canvas in CPU memory.
 *   2. at_texture_atlas_add / _add_file for each image: each returns the
 *      index of its region (or -1 if it does not fit).
 *   3. at_texture_atlas_build: uploads the canvas as a texture. No images
 *      can be added after this.
 *   4. at_texture_atlas_sprite(atlas, region, position) to draw.
 *
 * Images are placed in rows ("shelves"), left to right, top to bottom, in
 * the order they are added. Adding the tallest images first wastes less
 * space.
 *
 * Padding: with linear filtering, or when sprites are scaled or turned, the
 * GPU may read texels just outside a region. Each image is surrounded by
 * `padding` texels that repeat its edge, so nothing from the neighbours
 * bleeds in. 1 is enough for most cases; 0 packs tighter for pixel art
 * that is never scaled by fractions.
 */

/* Opaque atlas handle. */
typedef struct at_texture_atlas at_texture_atlas;

/* Creates an empty width x height canvas. Returns NULL on failure. */
AT_API at_texture_atlas *at_texture_atlas_create(uint32_t width, uint32_t height, uint32_t padding);

/* Destroys the atlas and its texture. Passing NULL is allowed. renderer may */
/* be NULL if the atlas was never built.                                     */
AT_API void at_texture_atlas_destroy(at_texture_atlas *atlas, at_renderer *renderer);

/* Copies an image into the atlas: RGBA pixels, 4 bytes each, rows from top */
/* to bottom. Returns the region index, or -1 if it does not fit or the     */
/* atlas was already built.                                                 */
AT_API int32_t at_texture_atlas_add(at_texture_atlas *atlas, const void *pixels,
                                    uint32_t width, uint32_t height);

/* Loads an image file (PNG, JPG...) into the atlas. Relative paths are    */
/* resolved from the executable's folder. Returns the region index, or -1. */
AT_API int32_t at_texture_atlas_add_file(at_texture_atlas *atlas, const char *path);

/* Creates the atlas texture from the canvas and frees the canvas.  */
/* options may be NULL. Returns false on failure.                   */
AT_API bool at_texture_atlas_build(at_texture_atlas *atlas, at_renderer *renderer,
                                   const at_texture_options *options);

/* The atlas texture, or NULL before at_texture_atlas_build. */
AT_API at_texture *at_texture_atlas_get_texture(const at_texture_atlas *atlas);

/* Number of regions (images added so far). */
AT_API uint32_t at_texture_atlas_get_region_count(const at_texture_atlas *atlas);

/* Rectangle of a region in texels, ready for at_sprite_desc.source. */
/* An unknown index gives an empty rectangle.                        */
AT_API at_rect at_texture_atlas_get_region(const at_texture_atlas *atlas, uint32_t region);

/* Size of the atlas texture in texels. */
AT_API void at_texture_atlas_get_size(const at_texture_atlas *atlas,
                                      uint32_t *width, uint32_t *height);

/* A sprite of one region at position, with the defaults of at_sprite_desc_make. */
static inline at_sprite_desc at_texture_atlas_sprite(const at_texture_atlas *atlas,
                                                     uint32_t region, at_vec2 position) {
    at_sprite_desc sprite = at_sprite_desc_make(at_texture_atlas_get_texture(atlas), position);
    sprite.source = at_texture_atlas_get_region(atlas, region);
    return sprite;
}

AT_EXTERN_C_END

#endif /* ARCHTYPE_RENDERER_TEXTURE_ATLAS_H */