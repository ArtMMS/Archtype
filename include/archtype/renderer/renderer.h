#ifndef ARCHTYPE_RENDERER_RENDERER_H
#define ARCHTYPE_RENDERER_RENDERER_H

#include "archtype/core/defines.h"
#include "archtype/platform/window.h"

AT_EXTERN_C_BEGIN

/* Opaque renderer handle. */
typedef struct at_renderer at_renderer;

typedef struct at_renderer_desc {
    at_window  *window;            /* Window the renderer draws into (required)     */
    const char *app_name;          /* NULL uses "Archtype Application"             */
    bool        enable_validation; /* Vulkan validation layers (for debugging)     */
    bool        vsync;             /* Sync with the monitor refresh (no tearing)   */
} at_renderer_desc;

/* Creates the renderer. Returns NULL on failure. */
AT_API at_renderer *at_renderer_create(const at_renderer_desc *desc);

/* Destroys the renderer. Passing NULL is allowed. */
AT_API void at_renderer_destroy(at_renderer *renderer);

AT_EXTERN_C_END

#endif /* ARCHTYPE_RENDERER_RENDERER_H */