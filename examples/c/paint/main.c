/*
 * Paint example
 *   Left mouse button:  draw a trail that keeps changing color
 *   Right mouse button: clear everything
 */

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include <archtype/archtype.h>

#define WINDOW_TITLE "Archtype - Paint (left: draw, right: clear)"

#define MAX_POINTS         20000  /* Brush stamps kept on screen          */
#define BRUSH_RADIUS       8.0f   /* Pixels                               */
#define BRUSH_SPACING      2.0f   /* Pixels between stamps along a stroke */
#define HUE_STEP           0.002f /* Color change per stamp (0..1 = full) */
#define VERTICES_PER_POINT 6      /* Two triangles per stamp              */

/* Must match the inputs of brush.vert */
typedef struct vertex {
    float position[2]; /* Clip space                     */
    float local[2];    /* -1..1, used to round the stamp */
    float color[3];
} vertex;

/* One brush stamp, in window pixels */
typedef struct point {
    float x, y;
    float color[3];
} point;

typedef struct paint_state {
    point    points[MAX_POINTS];
    vertex   vertices[MAX_POINTS * VERTICES_PER_POINT];
    uint32_t point_count;
    uint32_t uploaded_count; /* Points already copied to the GPU buffer */
    float    hue;
    float    last_x, last_y;
    bool     drawing;
    bool     full_warned;
} paint_state;

/* Static: too big for the stack */
static paint_state s_paint;

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

/* Hue (0..1) at full saturation and brightness -> RGB */
static void hue_to_rgb(float hue, float rgb[3]) {
    float h = hue * 6.0f;
    float x = 1.0f - fabsf(fmodf(h, 2.0f) - 1.0f);

    float r = 0.0f, g = 0.0f, b = 0.0f;
    switch ((int)h % 6) {
        case 0: r = 1.0f; g = x;    b = 0.0f; break;
        case 1: r = x;    g = 1.0f; b = 0.0f; break;
        case 2: r = 0.0f; g = 1.0f; b = x;    break;
        case 3: r = 0.0f; g = x;    b = 1.0f; break;
        case 4: r = x;    g = 0.0f; b = 1.0f; break;
        default: r = 1.0f; g = 0.0f; b = x;   break;
    }

    rgb[0] = r;
    rgb[1] = g;
    rgb[2] = b;
}

static void add_point(float x, float y) {
    if (s_paint.point_count >= MAX_POINTS) {
        if (!s_paint.full_warned) {
            printf("Canvas is full (%d stamps): right click to clear\n", MAX_POINTS);
            s_paint.full_warned = true;
        }
        return;
    }

    point *p = &s_paint.points[s_paint.point_count++];
    p->x = x;
    p->y = y;
    hue_to_rgb(s_paint.hue, p->color);

    s_paint.hue += HUE_STEP;
    if (s_paint.hue >= 1.0f) {
        s_paint.hue -= 1.0f;
    }
}

/* Stamps evenly along the line from the last stamp to (x, y), so fast */
/* mouse movements still give a continuous stroke.                     */
static void add_stroke_to(float x, float y) {
    float dx       = x - s_paint.last_x;
    float dy       = y - s_paint.last_y;
    float distance = sqrtf(dx * dx + dy * dy);
    int   steps    = (int)(distance / BRUSH_SPACING);

    for (int i = 1; i <= steps; ++i) {
        float t = (float)i * BRUSH_SPACING / distance;
        add_point(s_paint.last_x + dx * t, s_paint.last_y + dy * t);
    }

    /* Continue from the last stamp, not the mouse, to keep spacing even */
    if (steps > 0) {
        float t = (float)steps * BRUSH_SPACING / distance;
        s_paint.last_x += dx * t;
        s_paint.last_y += dy * t;
    }
}

/* Builds the 6 vertices (2 triangles) of a stamp.                   */
/* Pixels -> clip space: Vulkan's Y points down, like window pixels. */
static void build_vertices(const point *p, vertex *out, float width, float height) {
    static const float corners[4][2] = {
        { -1.0f, -1.0f }, /* top left     */
        { -1.0f,  1.0f }, /* bottom left  */
        {  1.0f,  1.0f }, /* bottom right */
        {  1.0f, -1.0f }, /* top right    */
    };
    static const int order[VERTICES_PER_POINT] = { 0, 1, 2, 2, 3, 0 };

    for (int i = 0; i < VERTICES_PER_POINT; ++i) {
        const float *corner = corners[order[i]];
        float        px     = p->x + corner[0] * BRUSH_RADIUS;
        float        py     = p->y + corner[1] * BRUSH_RADIUS;

        out[i].position[0] = px / width  * 2.0f - 1.0f;
        out[i].position[1] = py / height * 2.0f - 1.0f;
        out[i].local[0]    = corner[0];
        out[i].local[1]    = corner[1];
        out[i].color[0]    = p->color[0];
        out[i].color[1]    = p->color[1];
        out[i].color[2]    = p->color[2];
    }
}

/* Sends the stamps added since the last upload to the GPU buffer */
static void upload_new_points(at_renderer *renderer, at_buffer *buffer,
                              float width, float height) {
    uint32_t first = s_paint.uploaded_count;
    uint32_t count = s_paint.point_count - first;
    if (count == 0) {
        return;
    }

    vertex *vertices = &s_paint.vertices[first * VERTICES_PER_POINT];
    for (uint32_t i = 0; i < count; ++i) {
        build_vertices(&s_paint.points[first + i], &vertices[i * VERTICES_PER_POINT],
                       width, height);
    }

    size_t offset = (size_t)first * VERTICES_PER_POINT * sizeof(vertex);
    size_t size   = (size_t)count * VERTICES_PER_POINT * sizeof(vertex);

    if (at_buffer_update(renderer, buffer, vertices, size, offset)) {
        s_paint.uploaded_count = s_paint.point_count;
    }
}

static void handle_mouse(at_window *window) {
    if (at_window_is_mouse_button_down(window, AT_MOUSE_BUTTON_RIGHT)) {
        /* Clear: nothing to upload, the next draw just uses 0 stamps */
        s_paint.point_count    = 0;
        s_paint.uploaded_count = 0;
        s_paint.drawing        = false;
        s_paint.full_warned    = false;
        return;
    }

    if (!at_window_is_mouse_button_down(window, AT_MOUSE_BUTTON_LEFT)) {
        s_paint.drawing = false;
        return;
    }

    float x = 0.0f;
    float y = 0.0f;
    at_window_get_mouse_position(window, &x, &y);

    if (!s_paint.drawing) {
        /* New stroke: start with a single stamp */
        add_point(x, y);
        s_paint.last_x  = x;
        s_paint.last_y  = y;
        s_paint.drawing = true;
    } else {
        add_stroke_to(x, y);
    }
}

/* ------------------------------------------------------------------------ */
/* Main                                                                      */
/* ------------------------------------------------------------------------ */

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
        .app_name          = "Paint Example",
        .enable_validation = true,
        .vsync             = true,
    };

    at_renderer *renderer = at_renderer_create(&renderer_desc);
    if (!renderer) {
        at_window_destroy(window);
        at_platform_shutdown();
        return 1;
    }

    /* One buffer big enough for every stamp, filled as the user draws */
    at_buffer_desc buffer_desc = {
        .type = AT_BUFFER_VERTEX,
        .size = sizeof(s_paint.vertices),
    };
    at_buffer *vertex_buffer = at_buffer_create(renderer, &buffer_desc);

    at_shader *vertex_shader   = at_shader_load(renderer, "shaders/brush.vert.spv");
    at_shader *fragment_shader = at_shader_load(renderer, "shaders/brush.frag.spv");

    at_pipeline *pipeline = NULL;
    if (vertex_shader && fragment_shader) {
        at_pipeline_desc pipeline_desc = {
            .vertex_shader   = vertex_shader,
            .fragment_shader = fragment_shader,
            .vertex_layout   = {
                .stride          = sizeof(vertex),
                .attribute_count = 3,
                .attributes      = {
                    { .location = 0, .format = AT_VERTEX_FORMAT_FLOAT2,
                      .offset = offsetof(vertex, position) },
                    { .location = 1, .format = AT_VERTEX_FORMAT_FLOAT2,
                      .offset = offsetof(vertex, local) },
                    { .location = 2, .format = AT_VERTEX_FORMAT_FLOAT3,
                      .offset = offsetof(vertex, color) },
                },
            },
        };
        pipeline = at_pipeline_create(renderer, &pipeline_desc);
    }

    at_shader_destroy(renderer, vertex_shader);
    at_shader_destroy(renderer, fragment_shader);

    if (pipeline && vertex_buffer) {
        at_renderer_set_clear_color(renderer, 0.08f, 0.08f, 0.10f, 1.0f);

        int32_t built_width  = 0;
        int32_t built_height = 0;

        while (!at_window_should_close(window)) {
            at_platform_poll_events();

            if (at_window_is_minimized(window)) {
                at_platform_wait_events();
                continue;
            }

            handle_mouse(window);

            /* Vertices are in clip space, which depends on the window size: */
            /* after a resize, rebuild every stamp so circles stay round.    */
            int32_t width  = 0;
            int32_t height = 0;
            at_window_get_size(window, &width, &height);
            if (width != built_width || height != built_height) {
                built_width            = width;
                built_height           = height;
                s_paint.uploaded_count = 0;
            }

            upload_new_points(renderer, vertex_buffer, (float)width, (float)height);

            if (at_renderer_begin_frame(renderer)) {
                if (s_paint.uploaded_count > 0) {
                    at_renderer_bind_pipeline(renderer, pipeline);
                    at_renderer_bind_vertex_buffer(renderer, vertex_buffer);
                    at_renderer_draw(renderer, s_paint.uploaded_count * VERTICES_PER_POINT);
                }
                at_renderer_end_frame(renderer);
            }
        }
    }

    /* Destroy in reverse order of creation */
    at_pipeline_destroy(renderer, pipeline);
    at_buffer_destroy(renderer, vertex_buffer);
    at_renderer_destroy(renderer);
    at_window_destroy(window);
    at_platform_shutdown();
    return 0;
}