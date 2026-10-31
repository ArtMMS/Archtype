#ifndef ARCHTYPE_RENDERER_TEXTURE_H
#define ARCHTYPE_RENDERER_TEXTURE_H

#include "archtype/core/defines.h"
#include "archtype/renderer/renderer.h"

AT_EXTERN_C_BEGIN

/* How the texture looks when drawn bigger or smaller than its size. */
typedef enum at_texture_filter {
    AT_TEXTURE_FILTER_LINEAR = 0,  /* Smooth (default)                  */
    AT_TEXTURE_FILTER_NEAREST      /* Sharp pixels, ideal for pixel art */
} at_texture_filter;

/* What happens with texture coordinates outside 0..1. */
typedef enum at_texture_wrap {
    AT_TEXTURE_WRAP_REPEAT = 0,    /* Tile the texture (default)        */
    AT_TEXTURE_WRAP_CLAMP,         /* Stretch the edge pixels           */
    AT_TEXTURE_WRAP_MIRROR         /* Tile, flipping every other copy   */
} at_texture_wrap;

/* What the pixels contain. */
typedef enum at_texture_color_space {
    AT_TEXTURE_COLOR_SPACE_SRGB = 0, /* Colors, as in most images (default)  */
    AT_TEXTURE_COLOR_SPACE_LINEAR    /* Data: normal maps, masks, roughness  */
} at_texture_color_space;

/* Zero-initialize for the defaults, or pass NULL. */
typedef struct at_texture_options {
    at_texture_filter      filter;
    at_texture_wrap        wrap;
    at_texture_color_space color_space;
} at_texture_options;

/* Loads a texture from an image file (PNG, JPG, BMP, TGA...).           */
/* Relative paths are resolved from the executable's folder.             */
/* options may be NULL. Returns NULL on failure.                         */
AT_API at_texture *at_texture_load(at_renderer *renderer, const char *path,
                                   const at_texture_options *options);

/* Creates a texture from RGBA pixels in memory: 4 bytes per pixel,      */
/* rows from top to bottom. options may be NULL. Returns NULL on failure. */
AT_API at_texture *at_texture_create(at_renderer *renderer, uint32_t width, uint32_t height,
                                     const void *pixels, const at_texture_options *options);

/* Gets the texture size in pixels. */
AT_API void at_texture_get_size(const at_texture *texture, uint32_t *width, uint32_t *height);

/* Destroys a texture. Passing NULL is allowed.        */
/* Waits for the GPU, so avoid calling it every frame. */
AT_API void at_texture_destroy(at_renderer *renderer, at_texture *texture);

AT_EXTERN_C_END

#endif /* ARCHTYPE_RENDERER_TEXTURE_H */