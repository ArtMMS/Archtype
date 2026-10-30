#include <stdio.h>

#include <archtype/archtype.h>

int main(void) {
    if (!at_platform_init()) {
        return 1;
    }

    at_window_desc window_desc = {
        .title     = "Archtype - Renderer",
        .width     = 1280,
        .height    = 720,
        .resizable = true,
    };

    at_window *window = at_window_create(&window_desc);
    if (!window) {
        at_platform_shutdown();
        return 1;
    }

    at_renderer_desc renderer_desc = {
        .window            = window,
        .app_name          = "Renderer Example",
        .enable_validation = true,
        .vsync             = true,
    };

    at_renderer *renderer = at_renderer_create(&renderer_desc);
    if (!renderer) {
        at_window_destroy(window);
        at_platform_shutdown();
        return 1;
    }

    while (!at_window_should_close(window)) {
        at_platform_poll_events();

        if (at_window_is_minimized(window)) {
            at_platform_wait_events();
            continue;
        }

        /* Rendering will go here */
    }

    /* Destroy in reverse order of creation */
    at_renderer_destroy(renderer);
    at_window_destroy(window);
    at_platform_shutdown();
    return 0;
}