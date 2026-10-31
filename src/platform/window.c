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

/* SDL reports window coordinates; convert to framebuffer pixels, which */
/* differ on high-DPI displays.                                         */
static void set_mouse_position(at_window *window, float x, float y) {
    float density = SDL_GetWindowPixelDensity(window->handle);
    if (density <= 0.0f) {
        density = 1.0f;
    }
    window->mouse_x = x * density;
    window->mouse_y = y * density;
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

        case SDL_EVENT_MOUSE_MOTION:
            window = find_window(event->motion.windowID);
            if (window) {
                set_mouse_position(window, event->motion.x, event->motion.y);
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