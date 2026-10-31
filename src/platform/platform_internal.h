#ifndef ARCHTYPE_PLATFORM_INTERNAL_H
#define ARCHTYPE_PLATFORM_INTERNAL_H

/* Engine-only platform functions. Not part of the public API. */

/* volk.h first: it brings in the Vulkan types used below, and SDL_vulkan.h */
/* uses them instead of declaring its own.                                  */
#include <volk.h>

#include "archtype/core/defines.h"
#include "archtype/platform/window.h"

/* SDL's event type, declared here so this header does not need SDL. */
union SDL_Event;

/* ------------------------------------------------------------------------ */
/* Platform                                                                  */
/* ------------------------------------------------------------------------ */

/* Loads the Vulkan library and returns its vkGetInstanceProcAddr, or NULL  */
/* if Vulkan is not available. Every successful call must be balanced by   */
/* one call to at_platform_unload_vulkan.                                   */
PFN_vkGetInstanceProcAddr at_platform_load_vulkan(void);

/* Releases the Vulkan library loaded by at_platform_load_vulkan. */
void at_platform_unload_vulkan(void);

/* Returns the Vulkan instance extensions needed to draw into windows. */
/* The array is owned by the platform layer; do not free it.          */
const char *const *at_platform_get_vulkan_extensions(uint32_t *count);

/* Turns a relative path into one relative to the executable's folder.  */
/* Absolute paths are copied unchanged. Returns false if out is too     */
/* small.                                                               */
bool at_platform_resolve_path(const char *path, char *out, size_t out_size);

/* Returns true once the system asked the program to quit (e.g. Ctrl+C). */
bool at_platform_quit_requested(void);

/* Number of the current batch of events: it goes up by one every time   */
/* at_platform_poll_events or at_platform_wait_events runs. Starts at 0, */
/* before the first batch.                                               */
uint64_t at_platform_event_batch(void);

/* ------------------------------------------------------------------------ */
/* Window                                                                    */
/* ------------------------------------------------------------------------ */

/* Updates the window an event belongs to (resize, close, mouse...). */
/* Called by the platform layer for every event it receives.          */
void at_window_handle_event(const union SDL_Event *event);

/* Creates a Vulkan surface for the window. Logs the error on failure. */
bool at_window_create_vulkan_surface(const at_window *window,
                                     VkInstance       instance,
                                     VkSurfaceKHR    *surface);

#endif /* ARCHTYPE_PLATFORM_INTERNAL_H */