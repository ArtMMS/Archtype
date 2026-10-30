#ifndef ARCHTYPE_RENDERER_BUFFER_H
#define ARCHTYPE_RENDERER_BUFFER_H

#include "archtype/core/defines.h"
#include "archtype/renderer/renderer.h"

AT_EXTERN_C_BEGIN

/* What the buffer holds. */
typedef enum at_buffer_type {
    AT_BUFFER_VERTEX = 0,          /* Vertex data (positions, colors, UVs...) */
    AT_BUFFER_INDEX                /* Indices into the vertex buffer          */
} at_buffer_type;

/* Size of each index in an index buffer. */
typedef enum at_index_type {
    AT_INDEX_UINT16 = 0,           /* Up to 65 535 vertices (default, smaller) */
    AT_INDEX_UINT32                /* Up to ~4 billion vertices                */
} at_index_type;

typedef struct at_buffer_desc {
    at_buffer_type type;
    at_index_type  index_type;     /* Index buffers only                       */
    size_t         size;           /* Size in bytes (required)                 */
    const void    *data;           /* Initial contents, or NULL to leave empty */
} at_buffer_desc;

/* Creates a buffer in GPU memory, optionally filled with desc->data. */
/* Returns NULL on failure.                                           */
AT_API at_buffer *at_buffer_create(at_renderer *renderer, const at_buffer_desc *desc);

/* Replaces size bytes of the buffer, starting at offset.            */
/* Waits for the GPU, so it is meant for occasional changes, not for */
/* data that changes every frame.                                    */
AT_API bool at_buffer_update(at_renderer *renderer, at_buffer *buffer,
                             const void *data, size_t size, size_t offset);

/* Destroys a buffer. Passing NULL is allowed.         */
/* Waits for the GPU, so avoid calling it every frame. */
AT_API void at_buffer_destroy(at_renderer *renderer, at_buffer *buffer);

AT_EXTERN_C_END

#endif /* ARCHTYPE_RENDERER_BUFFER_H */