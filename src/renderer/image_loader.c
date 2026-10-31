#include "renderer/image_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stb_image.h>

#include "core/file.h"

bool at_image_load(const char *path, at_image_data *image) {
    memset(image, 0, sizeof(*image));

    /* Read the file ourselves so paths are resolved like every other asset */
    size_t size = 0;
    void  *file = at_file_read_all(path, &size);
    if (!file) {
        return false;
    }
    if (size > (size_t)INT32_MAX) {
        fprintf(stderr, "[archtype] Image file is too large: %s\n", path);
        free(file);
        return false;
    }

    int width    = 0;
    int height   = 0;
    int channels = 0;

    /* Always ask for 4 channels (RGBA), whatever the file contains */
    stbi_uc *pixels = stbi_load_from_memory((const stbi_uc *)file, (int)size,
                                            &width, &height, &channels, 4);
    free(file);

    if (!pixels) {
        fprintf(stderr, "[archtype] Could not decode image %s: %s\n", path,
                stbi_failure_reason());
        return false;
    }

    image->width  = (uint32_t)width;
    image->height = (uint32_t)height;
    image->pixels = pixels;
    return true;
}

void at_image_free(at_image_data *image) {
    if (image->pixels) {
        stbi_image_free(image->pixels);
    }
    memset(image, 0, sizeof(*image));
}