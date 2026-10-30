#ifndef ARCHTYPE_RENDERER_SHADER_H
#define ARCHTYPE_RENDERER_SHADER_H

#include "archtype/core/defines.h"
#include "archtype/renderer/renderer.h"

AT_EXTERN_C_BEGIN

/* Opaque shader handle: one compiled shader stage (vertex, fragment...). */
typedef struct at_shader at_shader;

/* Loads a compiled SPIR-V shader (.spv) from a file.                */
/* Relative paths are resolved from the executable's folder, e.g.    */
/* "shaders/triangle.vert.spv". Returns NULL on failure.             */
AT_API at_shader *at_shader_load(at_renderer *renderer, const char *path);

/* Creates a shader from SPIR-V code already in memory.              */
/* size is in bytes and must be a multiple of 4.                     */
AT_API at_shader *at_shader_create(at_renderer *renderer, const uint32_t *code, size_t size);

/* Destroys a shader. Passing NULL is allowed. Shaders are only      */
/* needed to create pipelines, so they can be destroyed right after. */
AT_API void at_shader_destroy(at_renderer *renderer, at_shader *shader);

AT_EXTERN_C_END

#endif /* ARCHTYPE_RENDERER_SHADER_H */