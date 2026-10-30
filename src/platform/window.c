#include "archtype/platform/window.h"
#include "platform/platform_internal.h"

#include <stdio.h>
#include <stdlib.h>

#include <GLFW/glfw3.h>

struct at_window {
    GLFWwindow *handle;
    int32_t     width;   /* Framebuffer width in pixels  */
    int32_t     height;  /* Framebuffer height in pixels */
    bool        resized;
};

static void framebuffer_size_callback(GLFWwindow *handle, int width, int height) {
    at_window *window = (at_window *)glfwGetWindowUserPointer(handle);
    if (!window) {
        return;
    }

    window->width   = (int32_t)width;
    window->height  = (int32_t)height;
    window->resized = true;
}

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

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); /* Vulkan, no OpenGL context */
    glfwWindowHint(GLFW_RESIZABLE, desc->resizable ? GLFW_TRUE : GLFW_FALSE);

    const char *title = desc->title ? desc->title : "Archtype";

    window->handle = glfwCreateWindow(desc->width, desc->height, title, NULL, NULL);
    if (!window->handle) {
        fprintf(stderr, "[archtype] Failed to create window\n");
        free(window);
        return NULL;
    }

    glfwSetWindowUserPointer(window->handle, window);
    glfwSetFramebufferSizeCallback(window->handle, framebuffer_size_callback);

    /* Keep a click reported as "down" until it is polled at least once, */
    /* so very quick clicks between two frames are not lost.            */
    glfwSetInputMode(window->handle, GLFW_STICKY_MOUSE_BUTTONS, GLFW_TRUE);

    int width  = 0;
    int height = 0;
    glfwGetFramebufferSize(window->handle, &width, &height);
    window->width  = (int32_t)width;
    window->height = (int32_t)height;

    return window;
}

void at_window_destroy(at_window *window) {
    if (!window) {
        return;
    }

    glfwDestroyWindow(window->handle);
    free(window);
}

bool at_window_should_close(const at_window *window) {
    return glfwWindowShouldClose(window->handle) == GLFW_TRUE;
}

void at_window_request_close(at_window *window) {
    glfwSetWindowShouldClose(window->handle, GLFW_TRUE);
}

void at_window_set_title(at_window *window, const char *title) {
    glfwSetWindowTitle(window->handle, title ? title : "Archtype");
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
    return glfwGetWindowAttrib(window->handle, GLFW_ICONIFIED) == GLFW_TRUE
        || window->width == 0
        || window->height == 0;
}

void at_window_get_mouse_position(const at_window *window, float *x, float *y) {
    double cursor_x = 0.0;
    double cursor_y = 0.0;
    glfwGetCursorPos(window->handle, &cursor_x, &cursor_y);

    /* GLFW reports screen units; convert to framebuffer pixels, which */
    /* differ on high-DPI displays.                                    */
    int screen_width  = 0;
    int screen_height = 0;
    glfwGetWindowSize(window->handle, &screen_width, &screen_height);

    double scale_x = screen_width  > 0 ? (double)window->width  / screen_width  : 1.0;
    double scale_y = screen_height > 0 ? (double)window->height / screen_height : 1.0;

    if (x) {
        *x = (float)(cursor_x * scale_x);
    }
    if (y) {
        *y = (float)(cursor_y * scale_y);
    }
}

bool at_window_is_mouse_button_down(const at_window *window, at_mouse_button button) {
    int glfw_button = GLFW_MOUSE_BUTTON_LEFT;
    switch (button) {
        case AT_MOUSE_BUTTON_RIGHT:  glfw_button = GLFW_MOUSE_BUTTON_RIGHT;  break;
        case AT_MOUSE_BUTTON_MIDDLE: glfw_button = GLFW_MOUSE_BUTTON_MIDDLE; break;
        case AT_MOUSE_BUTTON_LEFT:
        default:                     glfw_button = GLFW_MOUSE_BUTTON_LEFT;   break;
    }
    return glfwGetMouseButton(window->handle, glfw_button) == GLFW_PRESS;
}

VkResult at_window_create_vulkan_surface(const at_window *window,
                                         VkInstance       instance,
                                         VkSurfaceKHR    *surface) {
    return glfwCreateWindowSurface(instance, window->handle, NULL, surface);
}