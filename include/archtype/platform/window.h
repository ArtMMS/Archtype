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

/* Keyboard keys, by their position on a US keyboard: AT_KEY_W is the key */
/* where W is on a US layout, whatever the user's layout prints on it, so */
/* WASD controls work on every keyboard.                                  */
typedef enum at_key {
    AT_KEY_UNKNOWN = 0,

    AT_KEY_A, AT_KEY_B, AT_KEY_C, AT_KEY_D, AT_KEY_E, AT_KEY_F, AT_KEY_G,
    AT_KEY_H, AT_KEY_I, AT_KEY_J, AT_KEY_K, AT_KEY_L, AT_KEY_M, AT_KEY_N,
    AT_KEY_O, AT_KEY_P, AT_KEY_Q, AT_KEY_R, AT_KEY_S, AT_KEY_T, AT_KEY_U,
    AT_KEY_V, AT_KEY_W, AT_KEY_X, AT_KEY_Y, AT_KEY_Z,

    AT_KEY_0, AT_KEY_1, AT_KEY_2, AT_KEY_3, AT_KEY_4,
    AT_KEY_5, AT_KEY_6, AT_KEY_7, AT_KEY_8, AT_KEY_9,

    AT_KEY_F1, AT_KEY_F2, AT_KEY_F3, AT_KEY_F4,  AT_KEY_F5,  AT_KEY_F6,
    AT_KEY_F7, AT_KEY_F8, AT_KEY_F9, AT_KEY_F10, AT_KEY_F11, AT_KEY_F12,

    AT_KEY_SPACE,
    AT_KEY_ENTER,
    AT_KEY_ESCAPE,
    AT_KEY_TAB,
    AT_KEY_BACKSPACE,
    AT_KEY_DELETE,
    AT_KEY_MINUS,   /* -  */
    AT_KEY_EQUALS,  /* =  */

    AT_KEY_LEFT,
    AT_KEY_RIGHT,
    AT_KEY_UP,
    AT_KEY_DOWN,

    AT_KEY_LEFT_SHIFT,
    AT_KEY_RIGHT_SHIFT,
    AT_KEY_LEFT_CTRL,
    AT_KEY_RIGHT_CTRL,
    AT_KEY_LEFT_ALT,
    AT_KEY_RIGHT_ALT,

    AT_KEY_COUNT
} at_key;

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
/* Keyboard (basic polling; updated by at_platform_poll_events)             */
/* ------------------------------------------------------------------------ */

/* Returns true while the key is held down. Keys are released when the */
/* window loses focus, so they never stay stuck.                       */
AT_API bool at_window_is_key_down(const at_window *window, at_key key);

/* Returns true if the key was pressed during the last batch of events  */
/* (the last at_platform_poll_events or at_platform_wait_events): once  */
/* per press, without the repeats of a key held down. For toggles and   */
/* actions such as "Tab switches the mode".                             */
AT_API bool at_window_was_key_pressed(const at_window *window, at_key key);

/* ------------------------------------------------------------------------ */
/* Mouse (basic polling; updated by at_platform_poll_events)                */
/* ------------------------------------------------------------------------ */

/* Gets the cursor position in framebuffer pixels, from the top-left      */
/* corner of the window (same units as at_window_get_size). The cursor    */
/* may be outside the window, so values can be negative or past the size. */
AT_API void at_window_get_mouse_position(const at_window *window, float *x, float *y);

/* Gets how far the mouse moved during the last batch of events, in      */
/* framebuffer pixels (+Y is down). Keeps working while the mouse is     */
/* captured, when the cursor itself doesn't move. 0 if it didn't move.   */
AT_API void at_window_get_mouse_delta(const at_window *window, float *dx, float *dy);

/* Returns true while the mouse button is held down. */
AT_API bool at_window_is_mouse_button_down(const at_window *window, at_mouse_button button);

/* Returns how far the mouse wheel turned during the last batch of events    */
/* (the last at_platform_poll_events or at_platform_wait_events), or 0.     */
/* Positive is away from the user (scroll up), negative is toward the user. */
/* One notch of a regular wheel is usually 1; touchpads give smaller steps. */
AT_API float at_window_get_mouse_wheel(const at_window *window);

/* Captures the mouse: hides the cursor and keeps it inside the window, */
/* while at_window_get_mouse_delta keeps reporting the movement, with   */
/* no limit at the window edges. For mouse look in 3D cameras.          */
AT_API void at_window_set_mouse_captured(at_window *window, bool captured);

/* Returns true while the mouse is captured. */
AT_API bool at_window_is_mouse_captured(const at_window *window);

AT_EXTERN_C_END

#endif /* ARCHTYPE_PLATFORM_WINDOW_H */