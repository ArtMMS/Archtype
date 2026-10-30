#ifndef ARCHTYPE_PLATFORM_WINDOW_H
#define ARCHTYPE_PLATFORM_WINDOW_H

#include "archtype/core/defines.h"

AT_EXTERN_C_BEGIN

/* Opaque window handle. */
typedef struct at_window at_window;

typedef struct at_window_desc {
    const char *title;     /* NULL uses "Archtype"           */
    int32_t     width;     /* Initial width in screen units  */
    int32_t     height;    /* Initial height in screen units */
    bool        resizable;
} at_window_desc;

/* Mouse buttons. */
typedef enum at_mouse_button {
    AT_MOUSE_BUTTON_LEFT = 0,
    AT_MOUSE_BUTTON_RIGHT,
    AT_MOUSE_BUTTON_MIDDLE
} at_mouse_button;

/* Creates a window. Returns NULL on failure. */
AT_API at_window *at_window_create(const at_window_desc *desc);

/* Destroys a window. Passing NULL is allowed. */
AT_API void at_window_destroy(at_window *window);

/* Returns true when the user asked to close the window. */
AT_API bool at_window_should_close(const at_window *window);

/* Asks the window to close (should_close will return true). */
AT_API void at_window_request_close(at_window *window);

/* Changes the window title. */
AT_API void at_window_set_title(at_window *window, const char *title);

/* Gets the framebuffer size in pixels (what the renderer draws into). */
AT_API void at_window_get_size(const at_window *window, int32_t *width, int32_t *height);

/* Returns true once after the framebuffer was resized, then resets. */
AT_API bool at_window_was_resized(at_window *window);

/* Returns true while the window is minimized. */
AT_API bool at_window_is_minimized(const at_window *window);

/* ------------------------------------------------------------------------ */
/* Mouse (basic polling; updated by at_platform_poll_events)                */
/* ------------------------------------------------------------------------ */

/* Gets the cursor position in framebuffer pixels, from the top-left      */
/* corner of the window (same units as at_window_get_size). The cursor    */
/* may be outside the window, so values can be negative or past the size. */
AT_API void at_window_get_mouse_position(const at_window *window, float *x, float *y);

/* Returns true while the mouse button is held down. */
AT_API bool at_window_is_mouse_button_down(const at_window *window, at_mouse_button button);

AT_EXTERN_C_END

#endif /* ARCHTYPE_PLATFORM_WINDOW_H */