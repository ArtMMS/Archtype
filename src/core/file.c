#include "core/file.h"

#include <stdio.h>
#include <stdlib.h>

#include "platform/platform_internal.h"

#define AT_MAX_PATH 1024

void *at_file_read_all(const char *path, size_t *size) {
    char full_path[AT_MAX_PATH];
    if (!at_platform_resolve_path(path, full_path, sizeof(full_path))) {
        fprintf(stderr, "[archtype] Path is too long: %s\n", path);
        return NULL;
    }

    FILE *file = fopen(full_path, "rb");
    if (!file) {
        fprintf(stderr, "[archtype] Could not open file: %s\n", full_path);
        return NULL;
    }

    void *data   = NULL;
    long  length = -1;

    if (fseek(file, 0, SEEK_END) == 0) {
        length = ftell(file);
    }

    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "[archtype] Could not get the size of file: %s\n", full_path);
        fclose(file);
        return NULL;
    }

    /* malloc(0) may return NULL, so always allocate at least 1 byte */
    data = malloc(length > 0 ? (size_t)length : 1);
    if (!data) {
        fprintf(stderr, "[archtype] Out of memory reading file: %s\n", full_path);
        fclose(file);
        return NULL;
    }

    if (fread(data, 1, (size_t)length, file) != (size_t)length) {
        fprintf(stderr, "[archtype] Could not read file: %s\n", full_path);
        free(data);
        fclose(file);
        return NULL;
    }

    fclose(file);
    *size = (size_t)length;
    return data;
}