#ifndef ARCHTYPE_CORE_FILE_H
#define ARCHTYPE_CORE_FILE_H

/* File helpers for the engine. Not part of the public API. */

#include "archtype/core/defines.h"

/* Reads a whole file into memory. Relative paths are resolved from the */
/* executable's folder. Returns NULL on failure; free the result with   */
/* free().                                                              */
void *at_file_read_all(const char *path, size_t *size);

#endif /* ARCHTYPE_CORE_FILE_H */