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

/* Sets the color the screen is cleared to at the start of each frame.   */
/* Components go from 0 to 1 and are sRGB, like a color picker value.    */
/* The default is black.                                                 */
AT_API void at_renderer_set_clear_color(at_renderer *renderer,
                                        float r, float g, float b, float a);

/* Starts a frame and clears the screen.                                  */
/* Returns false when the frame must be skipped: in that case, do not     */
/* draw and do not call at_renderer_end_frame.                            */
AT_API bool at_renderer_begin_frame(at_renderer *renderer);

/* Finishes the frame and shows it on the window. */
AT_API void at_renderer_end_frame(at_renderer *renderer);

AT_EXTERN_C_END

#endif /* ARCHTYPE_RENDERER_RENDERER_H */