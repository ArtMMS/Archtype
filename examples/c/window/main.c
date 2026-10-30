#include <stdio.h>

#include <archtype/archtype.h>

#define WINDOW_TITLE "Archtype - Window"

int main(void) {
    if (!at_platform_init()) {
        return 1;
    }

    at_window_desc desc = {
        .title     = WINDOW_TITLE,
        .width     = 1280,
        .height    = 720,
        .resizable = true,
    };

    at_window *window = at_window_create(&desc);
    if (!window) {
        at_platform_shutdown();
        return 1;
    }

    double   last_time   = at_platform_get_time();
    double   fps_timer   = 0.0;
    uint32_t frame_count = 0;

    /* Main loop */
    while (!at_window_should_close(window)) {
        at_platform_poll_events();

        /* Do not spin while minimized: sleep until something happens */
        if (at_window_is_minimized(window)) {
            at_platform_wait_events();
            last_time = at_platform_get_time();
            continue;
        }

        /* Delta time: seconds since the previous frame */
        double now        = at_platform_get_time();
        double delta_time = now - last_time;
        last_time         = now;

        if (at_window_was_resized(window)) {
            int32_t width  = 0;
            int32_t height = 0;
            at_window_get_size(window, &width, &height);
            printf("Window resized to %dx%d\n", (int)width, (int)height);
        }

        /* Update and render will go here */

        /* Show FPS and frame time in the title once per second */
        fps_timer += delta_time;
        frame_count++;

        if (fps_timer >= 1.0) {
            char title[128];
            snprintf(title, sizeof(title), WINDOW_TITLE " | %u FPS | %.3f ms",
                     (unsigned)frame_count, 1000.0 * fps_timer / frame_count);
            at_window_set_title(window, title);

            fps_timer   = 0.0;
            frame_count = 0;
        }
    }

    at_window_destroy(window);
    at_platform_shutdown();
    return 0;
}