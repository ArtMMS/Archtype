#include "archtype/platform/platform.h"

#include <stdio.h>

#include <GLFW/glfw3.h>

static bool s_initialized = false;

static void glfw_error_callback(int code, const char *description) {
    fprintf(stderr, "[archtype] GLFW error %d: %s\n", code, description);
}

bool at_platform_init(void) {
    if (s_initialized) {
        return true;
    }

    glfwSetErrorCallback(glfw_error_callback);

    if (!glfwInit()) {
        fprintf(stderr, "[archtype] Failed to initialize GLFW\n");
        return false;
    }

    s_initialized = true;
    return true;
}

void at_platform_shutdown(void) {
    if (!s_initialized) {
        return;
    }

    glfwTerminate();
    s_initialized = false;
}

void at_platform_poll_events(void) {
    glfwPollEvents();
}

void at_platform_wait_events(void) {
    glfwWaitEvents();
}

double at_platform_get_time(void) {
    return glfwGetTime();
}