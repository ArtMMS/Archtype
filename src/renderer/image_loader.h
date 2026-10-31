#ifndef ARCHTYPE_RENDERER_IMAGE_LOADER_H
#define ARCHTYPE_RENDERER_IMAGE_LOADER_H

/* Decodes image files (PNG, JPG, BMP, TGA...) into RGBA8 pixels.     */
/* This is the only file that knows which image library is used       */
/* (stb_image for now). Not part of the public API.                   */

#include "archtype/core/defines.h"

typedef struct at_image_data {
    uint32_t width;
    uint32_t height;
    uint8_t *pixels; /* width * height * 4 bytes, rows from top to bottom */
} at_image_data;

/* Loads an image file. Relative paths are resolved from the executable's */
/* folder. Free the result with at_image_free.                            */
bool at_image_load(const char *path, at_image_data *image);

void at_image_free(at_image_data *image);

#endif /* ARCHTYPE_RENDERER_IMAGE_LOADER_H */