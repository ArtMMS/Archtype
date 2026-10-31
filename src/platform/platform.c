#include "archtype/platform/platform.h"
#include "platform/platform_internal.h"

#include <stdio.h>

/* The game's own main() is the entry point: SDL must not replace it. */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_vulkan.h>

static bool     s_initialized       = false;
static bool     s_quit_requested    = false;
static uint64_t s_start_counter     = 0;
static uint64_t s_counter_frequency = 0;
static uint64_t s_event_batch       = 0;

static bool is_absolute_path(const char *path) {
    if (path[0] == '/' || path[0] == '\\') {
        return true;
    }
    /* Windows drive letter, e.g. "D:/..." */
    return path[0] != '\0' && path[1] == ':';
}

/* Quit requests are global; everything else belongs to a window. */
static void dispatch_event(const SDL_Event *event) {
    if (event->type == SDL_EVENT_QUIT) {
        s_quit_requested = true;
        return;
    }
    at_window_handle_event(event);
}

/* ------------------------------------------------------------------------ */
/* Public functions                                                          */
/* ------------------------------------------------------------------------ */

bool at_platform_init(void) {
    if (s_initialized) {
        return true;
    }

    /* We don't use SDL_main, so tell SDL that main() already ran */
    SDL_SetMainReady();

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "[archtype] Failed to initialize SDL: %s\n", SDL_GetError());
        return false;
    }

    s_start_counter     = SDL_GetPerformanceCounter();
    s_counter_frequency = SDL_GetPerformanceFrequency();
    s_quit_requested    = false;
    s_event_batch       = 0;
    s_initialized       = true;
    return true;
}

void at_platform_shutdown(void) {
    if (!s_initialized) {
        return;
    }

    SDL_Quit();
    s_initialized = false;
}

void at_platform_poll_events(void) {
    ++s_event_batch;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        dispatch_event(&event);
    }
}

void at_platform_wait_events(void) {
    ++s_event_batch;

    /* Sleep until one event arrives, then handle whatever else is queued */
    SDL_Event event;
    if (SDL_WaitEvent(&event)) {
        dispatch_event(&event);
        while (SDL_PollEvent(&event)) {
            dispatch_event(&event);
        }
    }
}

double at_platform_get_time(void) {
    if (s_counter_frequency == 0) {
        return 0.0;
    }
    return (double)(SDL_GetPerformanceCounter() - s_start_counter)
         / (double)s_counter_frequency;
}

const char *at_platform_get_base_path(void) {
    /* Owned and cached by SDL; always ends with a path separator */
    const char *path = SDL_GetBasePath();
    return path ? path : "";
}

/* ------------------------------------------------------------------------ */
/* Engine-only functions                                                     */
/* ------------------------------------------------------------------------ */

bool at_platform_resolve_path(const char *path, char *out, size_t out_size) {
    int written = is_absolute_path(path)
                ? snprintf(out, out_size, "%s", path)
                : snprintf(out, out_size, "%s%s", at_platform_get_base_path(), path);

    return written >= 0 && (size_t)written < out_size;
}

bool at_platform_quit_requested(void) {
    return s_quit_requested;
}

uint64_t at_platform_event_batch(void) {
    return s_event_batch;
}

PFN_vkGetInstanceProcAddr at_platform_load_vulkan(void) {
    /* NULL: use the system's Vulkan loader (vulkan-1.dll, libvulkan.so.1) */
    if (!SDL_Vulkan_LoadLibrary(NULL)) {
        fprintf(stderr, "[archtype] Could not load Vulkan: %s\n", SDL_GetError());
        return NULL;
    }

    PFN_vkGetInstanceProcAddr get_instance_proc_addr =
        (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
    if (!get_instance_proc_addr) {
        fprintf(stderr, "[archtype] Could not get vkGetInstanceProcAddr: %s\n",
                SDL_GetError());
        SDL_Vulkan_UnloadLibrary();
        return NULL;
    }

    return get_instance_proc_addr;
}

void at_platform_unload_vulkan(void) {
    SDL_Vulkan_UnloadLibrary();
}

const char *const *at_platform_get_vulkan_extensions(uint32_t *count) {
    const char *const *extensions = SDL_Vulkan_GetInstanceExtensions(count);
    if (!extensions) {
        fprintf(stderr, "[archtype] Could not get the Vulkan extensions: %s\n",
                SDL_GetError());
    }
    return extensions;
}