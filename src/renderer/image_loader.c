#include "renderer/image_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include "core/file.h"

#define AT_IMAGE_BYTES_PER_PIXEL 4

/* Copies an RGBA32 surface into a tightly packed buffer (SDL rows may have */
/* padding at the end). Returns NULL on failure; free it with free().       */
static uint8_t *copy_rgba_pixels(SDL_Surface *surface) {
    size_t row_size = (size_t)surface->w * AT_IMAGE_BYTES_PER_PIXEL;
    size_t size     = row_size * (size_t)surface->h;

    uint8_t *pixels = (uint8_t *)malloc(size);
    if (!pixels) {
        return NULL;
    }

    bool locked = false;
    if (SDL_MUSTLOCK(surface)) {
        if (!SDL_LockSurface(surface)) {
            free(pixels);
            return NULL;
        }
        locked = true;
    }

    const uint8_t *src = (const uint8_t *)surface->pixels;
    for (int y = 0; y < surface->h; ++y) {
        memcpy(pixels + (size_t)y * row_size, src + (size_t)y * (size_t)surface->pitch,
               row_size);
    }

    if (locked) {
        SDL_UnlockSurface(surface);
    }
    return pixels;
}

bool at_image_load(const char *path, at_image_data *image) {
    memset(image, 0, sizeof(*image));

    /* Read the file ourselves so paths are resolved like every other asset */
    size_t size = 0;
    void  *file = at_file_read_all(path, &size);
    if (!file) {
        return false;
    }

    SDL_IOStream *stream = SDL_IOFromConstMem(file, size);
    if (!stream) {
        fprintf(stderr, "[archtype] Could not read image %s: %s\n", path, SDL_GetError());
        free(file);
        return false;
    }

    /* true: the stream is closed after decoding (the memory stays ours) */
    SDL_Surface *decoded = IMG_Load_IO(stream, true);
    free(file);

    if (!decoded) {
        fprintf(stderr, "[archtype] Could not decode image %s: %s\n", path, SDL_GetError());
        return false;
    }

    /* Whatever the file holds (RGB, grayscale, palette...), convert it to */
    /* RGBA32: 4 bytes per pixel, in R, G, B, A byte order.                */
    SDL_Surface *rgba = SDL_ConvertSurface(decoded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(decoded);

    if (!rgba) {
        fprintf(stderr, "[archtype] Could not convert image %s to RGBA: %s\n", path,
                SDL_GetError());
        return false;
    }

    if (rgba->w <= 0 || rgba->h <= 0) {
        fprintf(stderr, "[archtype] Image has no pixels: %s\n", path);
        SDL_DestroySurface(rgba);
        return false;
    }

    uint8_t *pixels = copy_rgba_pixels(rgba);
    if (!pixels) {
        fprintf(stderr, "[archtype] Out of memory loading image: %s\n", path);
        SDL_DestroySurface(rgba);
        return false;
    }

    image->width  = (uint32_t)rgba->w;
    image->height = (uint32_t)rgba->h;
    image->pixels = pixels;

    SDL_DestroySurface(rgba);
    return true;
}

void at_image_free(at_image_data *image) {
    free(image->pixels);
    memset(image, 0, sizeof(*image));
}