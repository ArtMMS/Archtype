#include <stdio.h>

#include <archtype/archtype.h>

static int s_failures = 0;
static int s_checks   = 0;

#define TEST_EPSILON 1e-4f

#define CHECK(condition)                                                     \
    do {                                                                     \
        ++s_checks;                                                          \
        if (!(condition)) {                                                  \
            ++s_failures;                                                    \
            printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);    \
        }                                                                    \
    } while (0)

static bool near_f(float a, float b) {
    return at_nearly_equalf(a, b, TEST_EPSILON);
}

static bool near_vec3(at_vec3 a, at_vec3 b) {
    return near_f(a.x, b.x) && near_f(a.y, b.y) && near_f(a.z, b.z);
}

static bool near_mat4(at_mat4 a, at_mat4 b) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (!near_f(a.m[c][r], b.m[c][r])) {
                return false;
            }
        }
    }
    return true;
}

static void test_scalar(void) {
    CHECK(near_f(at_radians(180.0f), AT_PI));
    CHECK(near_f(at_degrees(AT_PI), 180.0f));
    CHECK(near_f(at_clampf(5.0f, 0.0f, 1.0f), 1.0f));
    CHECK(near_f(at_lerpf(0.0f, 10.0f, 0.25f), 2.5f));
}

static void test_vec(void) {
    at_vec3 x = at_vec3_right();
    at_vec3 y = at_vec3_up();

    CHECK(near_vec3(at_vec3_cross(x, y), at_vec3_make(0.0f, 0.0f, 1.0f)));
    CHECK(near_f(at_vec3_dot(x, y), 0.0f));
    CHECK(near_f(at_vec3_length(at_vec3_make(3.0f, 4.0f, 0.0f)), 5.0f));
    CHECK(near_f(at_vec3_length(at_vec3_normalize(at_vec3_make(1.0f, 2.0f, 3.0f))), 1.0f));
    CHECK(near_vec3(at_vec3_normalize(at_vec3_zero()), at_vec3_zero()));
    CHECK(near_f(at_vec2_distance(at_vec2_make(1.0f, 1.0f), at_vec2_make(4.0f, 5.0f)), 5.0f));
}

static void test_mat4(void) {
    at_mat4 identity = at_mat4_identity();
    at_mat4 t = at_mat4_translate(at_vec3_make(1.0f, 2.0f, 3.0f));

    CHECK(near_mat4(at_mat4_mul(identity, t), t));
    CHECK(near_vec3(at_mat4_mul_point(t, at_vec3_zero()), at_vec3_make(1.0f, 2.0f, 3.0f)));
    CHECK(near_vec3(at_mat4_mul_direction(t, at_vec3_right()), at_vec3_right()));

    /* Order: b is applied first (scale, then translate) */
    at_mat4 s  = at_mat4_scale(at_vec3_make(2.0f, 2.0f, 2.0f));
    at_mat4 ts = at_mat4_mul(t, s);
    CHECK(near_vec3(at_mat4_mul_point(ts, at_vec3_one()), at_vec3_make(3.0f, 4.0f, 5.0f)));

    /* Rotating +X by 90 degrees around +Y gives -Z */
    at_mat4 rot = at_mat4_rotate(at_vec3_up(), at_radians(90.0f));
    CHECK(near_vec3(at_mat4_mul_direction(rot, at_vec3_right()), at_vec3_make(0.0f, 0.0f, -1.0f)));

    /* M * inverse(M) = identity */
    at_mat4 m = at_mat4_mul(t, at_mat4_mul(rot, s));
    CHECK(near_mat4(at_mat4_mul(m, at_mat4_inverse(m)), identity));
    CHECK(near_mat4(at_mat4_transpose(at_mat4_transpose(m)), m));
}

static void test_camera(void) {
    /* look_at: the camera position becomes the origin of view space */
    at_vec3 eye  = at_vec3_make(0.0f, 0.0f, 5.0f);
    at_mat4 view = at_mat4_look_at(eye, at_vec3_zero(), at_vec3_up());
    CHECK(near_vec3(at_mat4_mul_point(view, eye), at_vec3_zero()));
    CHECK(near_vec3(at_mat4_mul_point(view, at_vec3_zero()), at_vec3_make(0.0f, 0.0f, -5.0f)));

    /* Perspective: near plane -> depth 0, far plane -> depth 1 */
    at_mat4 proj = at_mat4_perspective(at_radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    at_vec4 near_clip = at_mat4_mul_vec4(proj, at_vec4_make(0.0f, 0.0f, -0.1f, 1.0f));
    at_vec4 far_clip  = at_mat4_mul_vec4(proj, at_vec4_make(0.0f, 0.0f, -100.0f, 1.0f));
    CHECK(near_f(near_clip.z / near_clip.w, 0.0f));
    CHECK(near_f(far_clip.z / far_clip.w, 1.0f));

    /* Perspective: a point above the center ends up at the top (Vulkan -Y) */
    at_vec4 above = at_mat4_mul_vec4(proj, at_vec4_make(0.0f, 1.0f, -5.0f, 1.0f));
    CHECK(above.y / above.w < 0.0f);

    /* Ortho in pixel coordinates (Y down): top-left -> (-1, -1) */
    at_mat4 ortho = at_mat4_ortho(0.0f, 800.0f, 600.0f, 0.0f, -1.0f, 1.0f);
    CHECK(near_vec3(at_mat4_mul_point(ortho, at_vec3_make(0.0f, 0.0f, 0.0f)),
                    at_vec3_make(-1.0f, -1.0f, 0.5f)));
    CHECK(near_vec3(at_mat4_mul_point(ortho, at_vec3_make(800.0f, 600.0f, 0.0f)),
                    at_vec3_make(1.0f, 1.0f, 0.5f)));
}

static void test_quat(void) {
    at_quat q = at_quat_from_axis_angle(at_vec3_up(), at_radians(90.0f));

    CHECK(near_f(at_quat_length(q), 1.0f));
    CHECK(near_vec3(at_quat_rotate_vec3(q, at_vec3_right()), at_vec3_make(0.0f, 0.0f, -1.0f)));

    /* Quaternion matrix matches the axis-angle matrix */
    at_mat4 from_quat = at_quat_to_mat4(q);
    at_mat4 from_axis = at_mat4_rotate(at_vec3_up(), at_radians(90.0f));
    CHECK(near_mat4(from_quat, from_axis));

    /* Two 45 degree rotations equal one 90 degree rotation */
    at_quat half = at_quat_from_axis_angle(at_vec3_up(), at_radians(45.0f));
    at_quat twice = at_quat_mul(half, half);
    CHECK(near_vec3(at_quat_rotate_vec3(twice, at_vec3_right()), at_vec3_make(0.0f, 0.0f, -1.0f)));

    /* q * inverse(q) = identity */
    at_quat id = at_quat_mul(q, at_quat_inverse(q));
    CHECK(near_f(id.w, 1.0f) && near_f(id.x, 0.0f) && near_f(id.y, 0.0f) && near_f(id.z, 0.0f));

    /* Slerp halfway between 0 and 90 degrees is 45 degrees */
    at_quat mid = at_quat_slerp(at_quat_identity(), q, 0.5f);
    CHECK(near_f(at_quat_dot(mid, half), 1.0f));

    /* Euler yaw of 90 degrees matches the axis-angle rotation */
    at_quat yaw = at_quat_from_euler(at_vec3_make(0.0f, at_radians(90.0f), 0.0f));
    CHECK(near_vec3(at_quat_rotate_vec3(yaw, at_vec3_right()), at_vec3_make(0.0f, 0.0f, -1.0f)));
}

static void test_transform(void) {
    at_transform t = at_transform_identity();
    CHECK(near_mat4(at_transform_to_mat4(t), at_mat4_identity()));

    t.position = at_vec3_make(10.0f, 0.0f, 0.0f);
    t.rotation = at_quat_from_axis_angle(at_vec3_up(), at_radians(90.0f));
    t.scale    = at_vec3_make(2.0f, 2.0f, 2.0f);

    /* Must match T * R * S built from separate matrices */
    at_mat4 expected = at_mat4_mul(at_mat4_translate(t.position),
                                   at_mat4_mul(at_quat_to_mat4(t.rotation),
                                               at_mat4_scale(t.scale)));
    CHECK(near_mat4(at_transform_to_mat4(t), expected));

    /* (1, 0, 0) -> scaled to (2, 0, 0) -> rotated to (0, 0, -2) -> moved to (10, 0, -2) */
    CHECK(near_vec3(at_mat4_mul_point(at_transform_to_mat4(t), at_vec3_right()),
                    at_vec3_make(10.0f, 0.0f, -2.0f)));

    /* Turning 90 degrees left: forward (-Z) becomes -X */
    CHECK(near_vec3(at_transform_forward(t), at_vec3_make(-1.0f, 0.0f, 0.0f)));
}

int main(void) {
    test_scalar();
    test_vec();
    test_mat4();
    test_camera();
    test_quat();
    test_transform();

    if (s_failures > 0) {
        printf("%d of %d checks failed\n", s_failures, s_checks);
        return 1;
    }

    printf("All %d checks passed\n", s_checks);
    return 0;
}