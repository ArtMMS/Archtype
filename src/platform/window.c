#include "archtype/platform/window.h"
#include "platform/platform_internal.h"

#include <stdio.h>
#include <stdlib.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

/* SDL window property that points back to our at_window */
#define AT_WINDOW_PROPERTY    "archtype.window"
#define AT_MOUSE_BUTTON_COUNT 3

struct at_window {
    SDL_Window *handle;
    int32_t     width;           /* Framebuffer width in pixels  */
    int32_t     height;          /* Framebuffer height in pixels */
    bool        resized;
    bool        close_requested;

    /* Mouse, in framebuffer pixels */
    float       mouse_x;
    float       mouse_y;
    bool        mouse_down[AT_MOUSE_BUTTON_COUNT];
    uint64_t    mouse_pressed_batch[AT_MOUSE_BUTTON_COUNT]; /* Event batch of the last press */
    float       mouse_wheel;       /* Wheel turned during mouse_wheel_batch */
    uint64_t    mouse_wheel_batch;
    float       mouse_dx;          /* Movement during mouse_delta_batch     */
    float       mouse_dy;
    uint64_t    mouse_delta_batch;
    bool        mouse_captured;

    /* Keyboard */
    bool        key_down[AT_KEY_COUNT];
    uint64_t    key_pressed_batch[AT_KEY_COUNT]; /* Event batch of the last press */
};

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

static at_window *find_window(SDL_WindowID id) {
    SDL_Window *handle = SDL_GetWindowFromID(id);
    if (!handle) {
        return NULL;
    }
    return (at_window *)SDL_GetPointerProperty(SDL_GetWindowProperties(handle),
                                               AT_WINDOW_PROPERTY, NULL);
}

static int to_button_index(Uint8 button) {
    switch (button) {
        case SDL_BUTTON_LEFT:   return AT_MOUSE_BUTTON_LEFT;
        case SDL_BUTTON_RIGHT:  return AT_MOUSE_BUTTON_RIGHT;
        case SDL_BUTTON_MIDDLE: return AT_MOUSE_BUTTON_MIDDLE;
        default:                return -1;
    }
}

/* Engine key for an SDL scancode (the key's physical position) */
static at_key to_key(SDL_Scancode scancode) {
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z) {
        return (at_key)(AT_KEY_A + (scancode - SDL_SCANCODE_A));
    }
    if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9) {
        return (at_key)(AT_KEY_1 + (scancode - SDL_SCANCODE_1));
    }
    if (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F12) {
        return (at_key)(AT_KEY_F1 + (scancode - SDL_SCANCODE_F1));
    }

    switch (scancode) {
        case SDL_SCANCODE_0:         return AT_KEY_0;
        case SDL_SCANCODE_SPACE:     return AT_KEY_SPACE;
        case SDL_SCANCODE_RETURN:    return AT_KEY_ENTER;
        case SDL_SCANCODE_KP_ENTER:  return AT_KEY_ENTER;
        case SDL_SCANCODE_ESCAPE:    return AT_KEY_ESCAPE;
        case SDL_SCANCODE_TAB:       return AT_KEY_TAB;
        case SDL_SCANCODE_BACKSPACE: return AT_KEY_BACKSPACE;
        case SDL_SCANCODE_DELETE:    return AT_KEY_DELETE;
        case SDL_SCANCODE_MINUS:     return AT_KEY_MINUS;
        case SDL_SCANCODE_EQUALS:    return AT_KEY_EQUALS;
        case SDL_SCANCODE_LEFT:      return AT_KEY_LEFT;
        case SDL_SCANCODE_RIGHT:     return AT_KEY_RIGHT;
        case SDL_SCANCODE_UP:        return AT_KEY_UP;
        case SDL_SCANCODE_DOWN:      return AT_KEY_DOWN;
        case SDL_SCANCODE_LSHIFT:    return AT_KEY_LEFT_SHIFT;
        case SDL_SCANCODE_RSHIFT:    return AT_KEY_RIGHT_SHIFT;
        case SDL_SCANCODE_LCTRL:     return AT_KEY_LEFT_CTRL;
        case SDL_SCANCODE_RCTRL:     return AT_KEY_RIGHT_CTRL;
        case SDL_SCANCODE_LALT:      return AT_KEY_LEFT_ALT;
        case SDL_SCANCODE_RALT:      return AT_KEY_RIGHT_ALT;
        default:                     return AT_KEY_UNKNOWN;
    }
}

static float pixel_density(const at_window *window) {
    float density = SDL_GetWindowPixelDensity(window->handle);
    return density > 0.0f ? density : 1.0f;
}

/* SDL reports window coordinates; convert to framebuffer pixels, which */
/* differ on high-DPI displays.                                         */
static void set_mouse_position(at_window *window, float x, float y) {
    float density   = pixel_density(window);
    window->mouse_x = x * density;
    window->mouse_y = y * density;
}

/* Adds to the movement of the current batch of events */
static void add_mouse_delta(at_window *window, float dx, float dy) {
    uint64_t batch = at_platform_event_batch();
    if (window->mouse_delta_batch != batch) {
        window->mouse_dx          = 0.0f;
        window->mouse_dy          = 0.0f;
        window->mouse_delta_batch = batch;
    }

    float density     = pixel_density(window);
    window->mouse_dx += dx * density;
    window->mouse_dy += dy * density;
}

/* Lets go of every key and button, e.g. when the window loses focus and */
/* would never see them being released.                                  */
static void release_all_input(at_window *window) {
    for (int i = 0; i < AT_KEY_COUNT; ++i) {
        window->key_down[i] = false;
    }
    for (int i = 0; i < AT_MOUSE_BUTTON_COUNT; ++i) {
        window->mouse_down[i] = false;
    }
}

/* ------------------------------------------------------------------------ */
/* Events (called by the platform layer)                                     */
/* ------------------------------------------------------------------------ */

void at_window_handle_event(const SDL_Event *event) {
    at_window *window = NULL;

    switch (event->type) {
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            window = find_window(event->window.windowID);
            if (window) {
                window->close_requested = true;
            }
            break;

        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            window = find_window(event->window.windowID);
            if (window) {
                window->width   = (int32_t)event->window.data1;
                window->height  = (int32_t)event->window.data2;
                window->resized = true;
            }
            break;

        case SDL_EVENT_WINDOW_FOCUS_LOST:
            window = find_window(event->window.windowID);
            if (window) {
                release_all_input(window);
            }
            break;

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            window     = find_window(event->key.windowID);
            at_key key = to_key(event->key.scancode);
            if (!window || key == AT_KEY_UNKNOWN) {
                break;
            }

            bool down = event->type == SDL_EVENT_KEY_DOWN;
            window->key_down[key] = down;

            /* Holding a key repeats KEY_DOWN events: only the first one is a press */
            if (down && !event->key.repeat) {
                window->key_pressed_batch[key] = at_platform_event_batch();
            }
            break;
        }

        case SDL_EVENT_MOUSE_MOTION:
            window = find_window(event->motion.windowID);
            if (window) {
                set_mouse_position(window, event->motion.x, event->motion.y);
                add_mouse_delta(window, event->motion.xrel, event->motion.yrel);
            }
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            window    = find_window(event->button.windowID);
            int index = to_button_index(event->button.button);
            if (!window || index < 0) {
                break;
            }

            bool down = event->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            window->mouse_down[index] = down;
            if (down) {
                window->mouse_pressed_batch[index] = at_platform_event_batch();
            }
            set_mouse_position(window, event->button.x, event->button.y);
            break;
        }

        case SDL_EVENT_MOUSE_WHEEL: {
            window = find_window(event->wheel.windowID);
            if (!window) {
                break;
            }

            /* First wheel event of this batch: forget the previous batch */
            uint64_t batch = at_platform_event_batch();
            if (window->mouse_wheel_batch != batch) {
                window->mouse_wheel       = 0.0f;
                window->mouse_wheel_batch = batch;
            }

            /* "Natural scrolling" flips the values: undo it, so positive */
            /* always means scrolling up                                  */
            float amount = event->wheel.y;
            if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                amount = -amount;
            }
            window->mouse_wheel += amount;

            set_mouse_position(window, event->wheel.mouse_x, event->wheel.mouse_y);
            break;
        }

        default:
            break;
    }
}

/* ------------------------------------------------------------------------ */
/* Public functions                                                          */
/* ------------------------------------------------------------------------ */

at_window *at_window_create(const at_window_desc *desc) {
    if (!desc || desc->width <= 0 || desc->height <= 0) {
        fprintf(stderr, "[archtype] Invalid window description\n");
        return NULL;
    }

    at_window *window = (at_window *)calloc(1, sizeof(*window));
    if (!window) {
        fprintf(stderr, "[archtype] Failed to allocate window\n");
        return NULL;
    }

    /* Vulkan window (no OpenGL context), drawn at the full pixel resolution */
    SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (desc->resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }

    const char *title = desc->title ? desc->title : "Archtype";

    window->handle = SDL_CreateWindow(title, desc->width, desc->height, flags);
    if (!window->handle) {
        fprintf(stderr, "[archtype] Failed to create window: %s\n", SDL_GetError());
        free(window);
        return NULL;
    }

    /* Lets events find this at_window from the SDL window */
    if (!SDL_SetPointerProperty(SDL_GetWindowProperties(window->handle),
                                AT_WINDOW_PROPERTY, window)) {
        fprintf(stderr, "[archtype] Failed to set up window: %s\n", SDL_GetError());
        SDL_DestroyWindow(window->handle);
        free(window);
        return NULL;
    }

    int width  = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window->handle, &width, &height);
    window->width  = (int32_t)width;
    window->height = (int32_t)height;

    return window;
}

void at_window_destroy(at_window *window) {
    if (!window) {
        return;
    }

    if (window->mouse_captured) {
        SDL_SetWindowRelativeMouseMode(window->handle, false);
    }
    SDL_DestroyWindow(window->handle);
    free(window);
}

bool at_window_should_close(const at_window *window) {
    return window->close_requested || at_platform_quit_requested();
}

void at_window_request_close(at_window *window) {
    window->close_requested = true;
}

void at_window_set_title(at_window *window, const char *title) {
    SDL_SetWindowTitle(window->handle, title ? title : "Archtype");
}

void at_window_get_size(const at_window *window, int32_t *width, int32_t *height) {
    if (width) {
        *width = window->width;
    }
    if (height) {
        *height = window->height;
    }
}

bool at_window_was_resized(at_window *window) {
    bool resized    = window->resized;
    window->resized = false;
    return resized;
}

bool at_window_is_minimized(const at_window *window) {
    return (SDL_GetWindowFlags(window->handle) & SDL_WINDOW_MINIMIZED) != 0
        || window->width == 0
        || window->height == 0;
}

bool at_window_is_key_down(const at_window *window, at_key key) {
    if ((int)key <= AT_KEY_UNKNOWN || (int)key >= AT_KEY_COUNT) {
        return false;
    }

    /* Like mouse buttons: a key pressed and released within the last batch */
    /* of events still counts as "down" once, so quick taps are not lost.   */
    uint64_t pressed = window->key_pressed_batch[key];
    return window->key_down[key] || (pressed != 0 && pressed == at_platform_event_batch());
}

bool at_window_was_key_pressed(const at_window *window, at_key key) {
    if ((int)key <= AT_KEY_UNKNOWN || (int)key >= AT_KEY_COUNT) {
        return false;
    }

    uint64_t pressed = window->key_pressed_batch[key];
    return pressed != 0 && pressed == at_platform_event_batch();
}

void at_window_get_mouse_position(const at_window *window, float *x, float *y) {
    if (x) {
        *x = window->mouse_x;
    }
    if (y) {
        *y = window->mouse_y;
    }
}

bool at_window_is_mouse_button_down(const at_window *window, at_mouse_button button) {
    int index = (int)button;
    if (index < 0 || index >= AT_MOUSE_BUTTON_COUNT) {
        return false;
    }

    /* A click pressed and released within the last batch of events still */
    /* counts as "down" once, so very quick clicks are not lost.           */
    uint64_t pressed = window->mouse_pressed_batch[index];
    return window->mouse_down[index]
        || (pressed != 0 && pressed == at_platform_event_batch());
}

void at_window_get_mouse_delta(const at_window *window, float *dx, float *dy) {
    /* Only the movement of the latest batch of events counts */
    bool current = window->mouse_delta_batch == at_platform_event_batch();
    if (dx) {
        *dx = current ? window->mouse_dx : 0.0f;
    }
    if (dy) {
        *dy = current ? window->mouse_dy : 0.0f;
    }
}

float at_window_get_mouse_wheel(const at_window *window) {
    /* Only the wheel turned in the latest batch of events counts */
    return window->mouse_wheel_batch == at_platform_event_batch() ? window->mouse_wheel : 0.0f;
}

void at_window_set_mouse_captured(at_window *window, bool captured) {
    if (window->mouse_captured == captured) {
        return;
    }

    /* Relative mode: hidden cursor, confined to the window, raw movement */
    if (!SDL_SetWindowRelativeMouseMode(window->handle, captured)) {
        fprintf(stderr, "[archtype] Failed to %s the mouse: %s\n",
                captured ? "capture" : "release", SDL_GetError());
        return;
    }
    window->mouse_captured = captured;
}

bool at_window_is_mouse_captured(const at_window *window) {
    return window->mouse_captured;
}

/* ------------------------------------------------------------------------ */
/* Engine-only functions                                                     */
/* ------------------------------------------------------------------------ */

bool at_window_create_vulkan_surface(const at_window *window,
                                     VkInstance       instance,
                                     VkSurfaceKHR    *surface) {
    if (!SDL_Vulkan_CreateSurface(window->handle, instance, NULL, surface)) {
        fprintf(stderr, "[archtype] Failed to create the Vulkan surface: %s\n",
                SDL_GetError());
        return false;
    }
    return true;
}