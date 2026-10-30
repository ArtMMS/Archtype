#include <math.h>
#include <stdio.h>

#include <archtype/archtype.h>

#define WINDOW_TITLE "Archtype - Renderer"

int main(void) {
    if (!at_platform_init()) {
        return 1;
    }

    at_window_desc window_desc = {
        .title     = WINDOW_TITLE,
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

    double   last_time   = at_platform_get_time();
    double   fps_timer   = 0.0;
    uint32_t frame_count = 0;

    while (!at_window_should_close(window)) {
        at_platform_poll_events();

        if (at_window_is_minimized(window)) {
            at_platform_wait_events();
            last_time = at_platform_get_time();
            continue;
        }

        double now        = at_platform_get_time();
        double delta_time = now - last_time;
        last_time         = now;

        /* Slowly cycle the clear color so we can see frames being drawn */
        float t = (float)now;
        float r = 0.5f + 0.5f * sinf(t * 0.7f);
        float g = 0.5f + 0.5f * sinf(t * 0.7f + 2.0f);
        float b = 0.5f + 0.5f * sinf(t * 0.7f + 4.0f);
        at_renderer_set_clear_color(renderer, r, g, b, 1.0f);

        if (at_renderer_begin_frame(renderer)) {
            /* Draw calls will go here */
            at_renderer_end_frame(renderer);
        }

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

    /* Destroy in reverse order of creation */
    at_renderer_destroy(renderer);
    at_window_destroy(window);
    at_platform_shutdown();
    return 0;
}