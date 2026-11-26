#include "archtype/renderer/camera2d.h"

/* Smallest zoom allowed: keeps the math away from dividing by zero */
#define AT_CAMERA2D_MIN_ZOOM 1e-4f

static float safe_zoom(const at_camera2d *camera) {
    return camera->zoom > AT_CAMERA2D_MIN_ZOOM ? camera->zoom : AT_CAMERA2D_MIN_ZOOM;
}

at_camera2d at_camera2d_make(void) {
    at_camera2d camera;
    camera.position = at_vec2_zero();
    camera.zoom     = 1.0f;
    camera.rotation = 0.0f;
    return camera;
}

at_mat4 at_camera2d_view_projection(const at_camera2d *camera,
                                    float viewport_width, float viewport_height) {
    float zoom        = safe_zoom(camera);
    float half_width  = viewport_width  * 0.5f / zoom;
    float half_height = viewport_height * 0.5f / zoom;

    /* Projection: the visible rectangle around the camera, +Y up. */
    /* Depth range -1..1 leaves room for layers later.             */
    at_mat4 projection = at_mat4_ortho(-half_width, half_width, -half_height, half_height,
                                       -1.0f, 1.0f);

    /* View: moves and turns the world the opposite way of the camera */
    at_mat4 translate = at_mat4_translate(at_vec3_make(-camera->position.x,
                                                       -camera->position.y, 0.0f));
    at_mat4 rotate    = at_mat4_rotate(at_vec3_make(0.0f, 0.0f, 1.0f), -camera->rotation);

    return at_mat4_mul(projection, at_mat4_mul(rotate, translate));
}

at_vec2 at_camera2d_screen_to_world(const at_camera2d *camera, at_vec2 screen,
                                    float viewport_width, float viewport_height) {
    float zoom = safe_zoom(camera);

    /* Offset from the screen center, in world units, with +Y up */
    float x = (screen.x - viewport_width  * 0.5f) / zoom;
    float y = (viewport_height * 0.5f - screen.y) / zoom;

    /* Turn with the camera, then move to its position */
    float c = cosf(camera->rotation);
    float s = sinf(camera->rotation);

    return at_vec2_make(camera->position.x + x * c - y * s,
                        camera->position.y + x * s + y * c);
}

at_vec2 at_camera2d_world_to_screen(const at_camera2d *camera, at_vec2 world,
                                    float viewport_width, float viewport_height) {
    float zoom = safe_zoom(camera);

    /* Offset from the camera, turned back by the camera rotation */
    float dx = world.x - camera->position.x;
    float dy = world.y - camera->position.y;
    float c  = cosf(camera->rotation);
    float s  = sinf(camera->rotation);
    float x  =  dx * c + dy * s;
    float y  = -dx * s + dy * c;

    return at_vec2_make(viewport_width  * 0.5f + x * zoom,
                        viewport_height * 0.5f - y * zoom);
}

void at_camera2d_zoom_at(at_camera2d *camera, at_vec2 screen, float factor,
                         float viewport_width, float viewport_height) {
    if (factor <= 0.0f) {
        return;
    }

    at_vec2 before = at_camera2d_screen_to_world(camera, screen,
                                                 viewport_width, viewport_height);

    camera->zoom = safe_zoom(camera) * factor;
    if (camera->zoom < AT_CAMERA2D_MIN_ZOOM) {
        camera->zoom = AT_CAMERA2D_MIN_ZOOM;
    }

    at_vec2 after = at_camera2d_screen_to_world(camera, screen,
                                                viewport_width, viewport_height);

    /* Move the camera so the same world point is under the screen position */
    camera->position = at_vec2_add(camera->position, at_vec2_sub(before, after));
}