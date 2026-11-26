#include "archtype/renderer/texture_atlas.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "renderer/image_loader.h"

#define AT_ATLAS_BYTES_PER_PIXEL 4

struct at_texture_atlas {
    uint32_t    width;
    uint32_t    height;
    uint32_t    padding;
    uint8_t    *pixels;  /* CPU canvas until build, then NULL */
    at_texture *texture; /* NULL until build                  */

    at_rect    *regions;
    uint32_t    region_count;
    uint32_t    region_capacity;

    /* Shelf packing: images go left to right on the current shelf; when */
    /* one doesn't fit, a new shelf starts below the tallest image.      */
    uint32_t    shelf_x;
    uint32_t    shelf_y;
    uint32_t    shelf_height;
};

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

static uint32_t clamp_index(int64_t value, uint32_t count) {
    if (value < 0) {
        return 0;
    }
    return value >= (int64_t)count ? count - 1 : (uint32_t)value;
}

/* Copies the image at (x, y) with `padding` texels around it that repeat */
/* its edge texels, so filtering never reads the neighbours' colors.      */
static void copy_with_padding(at_texture_atlas *atlas, const uint8_t *pixels,
                              uint32_t width, uint32_t height, uint32_t x, uint32_t y) {
    uint32_t padding = atlas->padding;

    for (uint32_t row = 0; row < height + 2 * padding; ++row) {
        uint32_t src_row = clamp_index((int64_t)row - (int64_t)padding, height);
        uint8_t *dst     = atlas->pixels
                         + ((size_t)(y + row) * atlas->width + x) * AT_ATLAS_BYTES_PER_PIXEL;

        for (uint32_t column = 0; column < width + 2 * padding; ++column) {
            uint32_t       src_column = clamp_index((int64_t)column - (int64_t)padding, width);
            const uint8_t *src        = pixels
                                      + ((size_t)src_row * width + src_column)
                                        * AT_ATLAS_BYTES_PER_PIXEL;
            memcpy(dst + (size_t)column * AT_ATLAS_BYTES_PER_PIXEL, src,
                   AT_ATLAS_BYTES_PER_PIXEL);
        }
    }
}

static bool grow_regions(at_texture_atlas *atlas) {
    if (atlas->region_count < atlas->region_capacity) {
        return true;
    }

    uint32_t capacity = atlas->region_capacity ? atlas->region_capacity * 2 : 16;
    at_rect *regions  = (at_rect *)realloc(atlas->regions, capacity * sizeof(*regions));
    if (!regions) {
        return false;
    }

    atlas->regions         = regions;
    atlas->region_capacity = capacity;
    return true;
}

/* ------------------------------------------------------------------------ */
/* Public functions                                                          */
/* ------------------------------------------------------------------------ */

at_texture_atlas *at_texture_atlas_create(uint32_t width, uint32_t height, uint32_t padding) {
    if (width == 0 || height == 0) {
        fprintf(stderr, "[archtype] Texture atlas size must not be 0\n");
        return NULL;
    }

    at_texture_atlas *atlas = (at_texture_atlas *)calloc(1, sizeof(*atlas));
    if (!atlas) {
        fprintf(stderr, "[archtype] Failed to allocate the texture atlas\n");
        return NULL;
    }

    /* Starts fully transparent */
    atlas->pixels = (uint8_t *)calloc((size_t)width * height, AT_ATLAS_BYTES_PER_PIXEL);
    if (!atlas->pixels) {
        fprintf(stderr, "[archtype] Out of memory for a %ux%u texture atlas\n", width, height);
        free(atlas);
        return NULL;
    }

    atlas->width   = width;
    atlas->height  = height;
    atlas->padding = padding;
    return atlas;
}

void at_texture_atlas_destroy(at_texture_atlas *atlas, at_renderer *renderer) {
    if (!atlas) {
        return;
    }

    if (atlas->texture && renderer) {
        at_texture_destroy(renderer, atlas->texture);
    }
    free(atlas->pixels);
    free(atlas->regions);
    free(atlas);
}

int32_t at_texture_atlas_add(at_texture_atlas *atlas, const void *pixels,
                             uint32_t width, uint32_t height) {
    if (!atlas || !pixels || width == 0 || height == 0) {
        fprintf(stderr, "[archtype] at_texture_atlas_add: pixels and a size are required\n");
        return -1;
    }
    if (!atlas->pixels) {
        fprintf(stderr, "[archtype] at_texture_atlas_add: the atlas was already built\n");
        return -1;
    }

    /* The space the image takes, with its padding */
    uint32_t padded_width  = width + 2 * atlas->padding;
    uint32_t padded_height = height + 2 * atlas->padding;

    /* Doesn't fit on this shelf: start a new one below */
    if (atlas->shelf_x + padded_width > atlas->width) {
        atlas->shelf_y     += atlas->shelf_height;
        atlas->shelf_x      = 0;
        atlas->shelf_height = 0;
    }

    if (padded_width > atlas->width || atlas->shelf_y + padded_height > atlas->height) {
        fprintf(stderr, "[archtype] Texture atlas is full: a %ux%u image does not fit "
                        "(atlas %ux%u)\n", width, height, atlas->width, atlas->height);
        return -1;
    }
    if (!grow_regions(atlas)) {
        fprintf(stderr, "[archtype] Out of memory adding to the texture atlas\n");
        return -1;
    }

    copy_with_padding(atlas, (const uint8_t *)pixels, width, height,
                      atlas->shelf_x, atlas->shelf_y);

    /* The region is the image itself, inside its padding */
    atlas->regions[atlas->region_count] =
        at_rect_make((float)(atlas->shelf_x + atlas->padding),
                     (float)(atlas->shelf_y + atlas->padding), (float)width, (float)height);

    atlas->shelf_x += padded_width;
    if (padded_height > atlas->shelf_height) {
        atlas->shelf_height = padded_height;
    }

    return (int32_t)atlas->region_count++;
}

int32_t at_texture_atlas_add_file(at_texture_atlas *atlas, const char *path) {
    if (!atlas || !path) {
        return -1;
    }

    at_image_data image;
    if (!at_image_load(path, &image)) {
        return -1;
    }

    int32_t region = at_texture_atlas_add(atlas, image.pixels, image.width, image.height);
    at_image_free(&image);
    return region;
}

bool at_texture_atlas_build(at_texture_atlas *atlas, at_renderer *renderer,
                            const at_texture_options *options) {
    if (!atlas || !renderer) {
        return false;
    }
    if (!atlas->pixels) {
        fprintf(stderr, "[archtype] at_texture_atlas_build: the atlas was already built\n");
        return false;
    }

    atlas->texture = at_texture_create(renderer, atlas->width, atlas->height, atlas->pixels,
                                       options);
    if (!atlas->texture) {
        return false;
    }

    /* The texture has a copy on the GPU now */
    free(atlas->pixels);
    atlas->pixels = NULL;

    fprintf(stderr, "[archtype] Texture atlas built: %u regions in %ux%u\n",
            atlas->region_count, atlas->width, atlas->height);
    return true;
}

at_texture *at_texture_atlas_get_texture(const at_texture_atlas *atlas) {
    return atlas ? atlas->texture : NULL;
}

uint32_t at_texture_atlas_get_region_count(const at_texture_atlas *atlas) {
    return atlas ? atlas->region_count : 0;
}

at_rect at_texture_atlas_get_region(const at_texture_atlas *atlas, uint32_t region) {
    if (!atlas || region >= atlas->region_count) {
        return at_rect_make(0.0f, 0.0f, 0.0f, 0.0f);
    }
    return atlas->regions[region];
}

void at_texture_atlas_get_size(const at_texture_atlas *atlas, uint32_t *width, uint32_t *height) {
    if (width) {
        *width = atlas ? atlas->width : 0;
    }
    if (height) {
        *height = atlas ? atlas->height : 0;
    }
}