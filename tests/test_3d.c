/* Tests for the 3D helpers that don't need a GPU: the perspective camera */
/* and the angles of the fly camera.                                      */

#include <math.h>
#include <stdio.h>

#include <archtype/archtype.h>

static int s_failures = 0;
static int s_checks   = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        ++s_checks;                                                          \
        if (!(condition)) {                                                  \
            ++s_failures;                                                    \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);    \
        }                                                                    \
    } while (0)

#define TOLERANCE 1e-4f

static bool near_vec3(at_vec3 a, at_vec3 b) {
    return at_nearly_equalf(a.x, b.x, TOLERANCE) && at_nearly_equalf(a.y, b.y, TOLERANCE)
        && at_nearly_equalf(a.z, b.z, TOLERANCE);
}

static bool near_mat4(at_mat4 a, at_mat4 b) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (!at_nearly_equalf(a.m[c][r], b.m[c][r], TOLERANCE)) {
                return false;
            }
        }
    }
    return true;
}

/* Depth (0..1) of a world point after the camera's projection */
static float depth_of(const at_camera3d *camera, at_vec3 point) {
    at_vec4 clip = at_mat4_mul_vec4(at_camera3d_view_projection(camera, 1280.0f, 720.0f),
                                    at_vec4_from_vec3(point, 1.0f));
    return clip.z / clip.w;
}

static void test_camera3d_defaults(void) {
    at_camera3d camera = at_camera3d_make();

    CHECK(near_mat4(at_camera3d_view(&camera), at_mat4_identity()));
    CHECK(near_vec3(at_camera3d_forward(&camera), at_vec3_make(0.0f, 0.0f, -1.0f)));
    CHECK(near_vec3(at_camera3d_right(&camera), at_vec3_make(1.0f, 0.0f, 0.0f)));
    CHECK(near_vec3(at_camera3d_up(&camera), at_vec3_make(0.0f, 1.0f, 0.0f)));

    /* Vulkan depth: 0 on the near plane, 1 on the far plane */
    CHECK(at_nearly_equalf(depth_of(&camera, at_vec3_make(0.0f, 0.0f, -camera.near_z)), 0.0f,
                           TOLERANCE));
    CHECK(at_nearly_equalf(depth_of(&camera, at_vec3_make(0.0f, 0.0f, -camera.far_z)), 1.0f,
                           TOLERANCE));
    float middle = depth_of(&camera, at_vec3_make(0.0f, 0.0f, -10.0f));
    CHECK(middle > 0.0f && middle < 1.0f);
}

static void test_camera3d_look_at(void) {
    at_camera3d camera = at_camera3d_make();
    camera.position    = at_vec3_make(3.0f, 4.0f, 5.0f);
    at_camera3d_look_at(&camera, at_vec3_zero(), at_vec3_up());

    at_vec3 expected = at_vec3_normalize(at_vec3_make(-3.0f, -4.0f, -5.0f));
    CHECK(near_vec3(at_camera3d_forward(&camera), expected));

    /* No roll: right stays level, up leans toward +Y */
    CHECK(at_nearly_equalf(at_camera3d_right(&camera).y, 0.0f, TOLERANCE));
    CHECK(at_camera3d_up(&camera).y > 0.0f);

    /* Same view as the look-at matrix of the math library */
    at_mat4 reference = at_mat4_look_at(camera.position, at_vec3_zero(), at_vec3_up());
    CHECK(near_mat4(at_camera3d_view(&camera), reference));

    /* Every direction of the basis */
    const at_vec3 targets[6] = {
        { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f },
        { 0.0f, 0.0f, -1.0f }, { 0.3f, 0.2f, 1.0f }, { -2.0f, -1.0f, 0.5f },
    };
    for (int i = 0; i < 6; ++i) {
        at_camera3d c = at_camera3d_make();
        at_camera3d_look_at(&c, targets[i], at_vec3_up());
        CHECK(near_vec3(at_camera3d_forward(&c), at_vec3_normalize(targets[i])));
    }

    /* Straight down still works (up is replaced) */
    at_camera3d down = at_camera3d_make();
    at_camera3d_look_at(&down, at_vec3_make(0.0f, -1.0f, 0.0f), at_vec3_up());
    CHECK(near_vec3(at_camera3d_forward(&down), at_vec3_make(0.0f, -1.0f, 0.0f)));

    /* Target at the camera: nothing changes */
    at_camera3d same = at_camera3d_make();
    at_camera3d_look_at(&same, at_vec3_zero(), at_vec3_up());
    CHECK(near_vec3(at_camera3d_forward(&same), at_vec3_make(0.0f, 0.0f, -1.0f)));
}

static void test_camera3d_screen(void) {
    const float width  = 1280.0f;
    const float height = 720.0f;

    at_camera3d camera = at_camera3d_make();
    camera.position    = at_vec3_make(0.0f, 2.0f, 10.0f);
    at_camera3d_look_at(&camera, at_vec3_make(0.0f, 2.0f, 0.0f), at_vec3_up());

    /* What the camera looks at is in the middle of the screen */
    at_vec2 screen = at_vec2_zero();
    CHECK(at_camera3d_world_to_screen(&camera, at_vec3_make(0.0f, 2.0f, 0.0f), width, height,
                                      &screen));
    CHECK(at_nearly_equalf(screen.x, width * 0.5f, 0.01f));
    CHECK(at_nearly_equalf(screen.y, height * 0.5f, 0.01f));

    /* Higher in the world is higher on screen (smaller Y), right is right */
    at_vec2 above = at_vec2_zero();
    at_vec2 right = at_vec2_zero();
    at_camera3d_world_to_screen(&camera, at_vec3_make(0.0f, 3.0f, 0.0f), width, height, &above);
    at_camera3d_world_to_screen(&camera, at_vec3_make(1.0f, 2.0f, 0.0f), width, height, &right);
    CHECK(above.y < height * 0.5f);
    CHECK(right.x > width * 0.5f);

    /* Behind the camera */
    at_vec2 untouched = at_vec2_make(-7.0f, -7.0f);
    CHECK(!at_camera3d_world_to_screen(&camera, at_vec3_make(0.0f, 2.0f, 20.0f), width, height,
                                       &untouched));
    CHECK(untouched.x == -7.0f && untouched.y == -7.0f);

    /* Ray through the center goes forward; a ray through any pixel hits */
    /* the world point drawn at that pixel                               */
    at_vec3 origin    = at_vec3_zero();
    at_vec3 direction = at_vec3_zero();
    at_camera3d_screen_to_ray(&camera, at_vec2_make(width * 0.5f, height * 0.5f), width, height,
                              &origin, &direction);
    CHECK(near_vec3(direction, at_camera3d_forward(&camera)));
    CHECK(at_nearly_equalf(at_vec3_distance(origin, camera.position), camera.near_z, 1e-3f));

    at_vec3 point = at_vec3_make(-2.5f, 0.5f, -3.0f);
    at_vec2 pixel = at_vec2_zero();
    at_camera3d_world_to_screen(&camera, point, width, height, &pixel);
    at_camera3d_screen_to_ray(&camera, pixel, width, height, &origin, &direction);
    at_vec3 to_point = at_vec3_normalize(at_vec3_sub(point, origin));
    CHECK(at_vec3_dot(to_point, direction) > 0.99999f);
}

static void test_fly_camera(void) {
    /* Yaw 0 looks down -Z; positive yaw turns left; positive pitch looks up */
    at_camera3d camera = at_camera3d_make();

    camera.rotation = at_fly_camera_rotation(0.0f, 0.0f);
    CHECK(near_vec3(at_camera3d_forward(&camera), at_vec3_make(0.0f, 0.0f, -1.0f)));

    camera.rotation = at_fly_camera_rotation(at_radians(90.0f), 0.0f);
    CHECK(near_vec3(at_camera3d_forward(&camera), at_vec3_make(-1.0f, 0.0f, 0.0f)));

    camera.rotation = at_fly_camera_rotation(0.0f, at_radians(30.0f));
    CHECK(at_camera3d_forward(&camera).y > 0.49f);

    /* Pitch never rolls the camera: right stays level */
    camera.rotation = at_fly_camera_rotation(at_radians(37.0f), at_radians(-50.0f));
    CHECK(at_nearly_equalf(at_camera3d_right(&camera).y, 0.0f, TOLERANCE));

    /* Sync reads back the same angles */
    at_fly_camera fly = at_fly_camera_make();
    at_fly_camera_sync(&fly, &camera);
    CHECK(at_nearly_equalf(fly.yaw, at_radians(37.0f), TOLERANCE));
    CHECK(at_nearly_equalf(fly.pitch, at_radians(-50.0f), TOLERANCE));

    /* ...and from a look-at */
    at_camera3d looking = at_camera3d_make();
    looking.position    = at_vec3_make(0.0f, 6.0f, 22.0f);
    at_camera3d_look_at(&looking, at_vec3_make(0.0f, 1.0f, 0.0f), at_vec3_up());
    at_fly_camera_sync(&fly, &looking);

    at_camera3d rebuilt = looking;
    rebuilt.rotation    = at_fly_camera_rotation(fly.yaw, fly.pitch);
    CHECK(near_vec3(at_camera3d_forward(&rebuilt), at_camera3d_forward(&looking)));
    CHECK(near_mat4(at_camera3d_view(&rebuilt), at_camera3d_view(&looking)));
}

int main(void) {
    test_camera3d_defaults();
    test_camera3d_look_at();
    test_camera3d_screen();
    test_fly_camera();

    if (s_failures > 0) {
        printf("%d of %d checks failed\n", s_failures, s_checks);
        return 1;
    }
    printf("All %d checks passed\n", s_checks);
    return 0;
}