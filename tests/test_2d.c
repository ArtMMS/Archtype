/* Tests for the 2D helpers that don't need a GPU: rectangles, sprite */
/* sheets, animations and the texture atlas packing.                  */

#include <stdio.h>
#include <string.h>

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

static bool same_rect(at_rect a, at_rect b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

static void test_rect(void) {
    at_rect r = at_rect_make(10.0f, 20.0f, 30.0f, 40.0f);

    CHECK(!at_rect_is_empty(r));
    CHECK(at_rect_is_empty(at_rect_make(0.0f, 0.0f, 0.0f, 5.0f)));
    CHECK(at_rect_contains(r, at_vec2_make(10.0f, 20.0f)));
    CHECK(at_rect_contains(r, at_vec2_make(40.0f, 60.0f)));
    CHECK(!at_rect_contains(r, at_vec2_make(41.0f, 30.0f)));
    CHECK(at_rect_center(r).x == 25.0f && at_rect_center(r).y == 40.0f);

    /* Touching edges don't overlap */
    CHECK(at_rect_overlaps(r, at_rect_make(39.0f, 59.0f, 5.0f, 5.0f)));
    CHECK(!at_rect_overlaps(r, at_rect_make(40.0f, 20.0f, 5.0f, 5.0f)));
}

static void test_sprite_sheet(void) {
    /* 4 x 2 frames of 16x16, margin 1, spacing 2: 72 x 36 texels */
    at_sprite_sheet sheet = at_sprite_sheet_make_sized(NULL, 72, 36, 16, 16, 1, 2);

    CHECK(sheet.columns == 4);
    CHECK(sheet.rows == 2);
    CHECK(at_sprite_sheet_frame_count(&sheet) == 8);

    CHECK(same_rect(at_sprite_sheet_frame(&sheet, 0), at_rect_make(1.0f, 1.0f, 16.0f, 16.0f)));
    CHECK(same_rect(at_sprite_sheet_frame(&sheet, 3), at_rect_make(55.0f, 1.0f, 16.0f, 16.0f)));
    CHECK(same_rect(at_sprite_sheet_frame(&sheet, 4), at_rect_make(1.0f, 19.0f, 16.0f, 16.0f)));

    /* Past the last frame wraps around */
    CHECK(same_rect(at_sprite_sheet_frame(&sheet, 9), at_sprite_sheet_frame(&sheet, 1)));

    /* Leftover texels that can't hold a whole frame are ignored */
    at_sprite_sheet partial = at_sprite_sheet_make_sized(NULL, 50, 20, 16, 16, 0, 0);
    CHECK(partial.columns == 3 && partial.rows == 1);

    /* Too small for one frame: no frames, empty rectangles */
    at_sprite_sheet tiny = at_sprite_sheet_make_sized(NULL, 8, 8, 16, 16, 0, 0);
    CHECK(at_sprite_sheet_frame_count(&tiny) == 0);
    CHECK(at_rect_is_empty(at_sprite_sheet_frame(&tiny, 0)));

    /* The sprite helper fills the source */
    at_sprite_desc sprite = at_sprite_sheet_sprite(&sheet, 5, at_vec2_make(3.0f, 4.0f));
    CHECK(same_rect(sprite.source, at_sprite_sheet_frame(&sheet, 5)));
    CHECK(sprite.position.x == 3.0f && sprite.tint.a == 1.0f);
}

static void test_animation(void) {
    at_animation walk = at_animation_make(4, 4, 8.0f, true); /* Frames 4..7, 0.5 s */

    CHECK(at_animation_frame(&walk, 0.0f) == 4);
    CHECK(at_animation_frame(&walk, 0.13f) == 5);   /* 1.04 frames in */
    CHECK(at_animation_frame(&walk, 0.49f) == 7);
    CHECK(at_animation_frame(&walk, 0.5f) == 4);    /* Loops */
    CHECK(at_animation_frame(&walk, -1.0f) == 4);
    CHECK(!at_animation_is_finished(&walk, 10.0f));
    CHECK(at_animation_duration(&walk) == 0.5f);

    at_animation once = at_animation_make(0, 3, 10.0f, false);
    CHECK(at_animation_frame(&once, 0.25f) == 2);
    CHECK(at_animation_frame(&once, 5.0f) == 2);    /* Stays on the last frame */
    CHECK(!at_animation_is_finished(&once, 0.25f));
    CHECK(at_animation_is_finished(&once, 0.3f));
}

static void test_texture_atlas(void) {
    static uint8_t pixels[64 * 64 * 4];
    memset(pixels, 255, sizeof(pixels));

    /* 64x64 atlas, padding 1: a 30x20 image takes 32x22 */
    at_texture_atlas *atlas = at_texture_atlas_create(64, 64, 1);
    CHECK(atlas != NULL);
    if (!atlas) {
        return;
    }

    int32_t a = at_texture_atlas_add(atlas, pixels, 30, 20); /* Shelf 1, left  */
    int32_t b = at_texture_atlas_add(atlas, pixels, 30, 10); /* Shelf 1, right */
    int32_t c = at_texture_atlas_add(atlas, pixels, 40, 30); /* Shelf 2        */
    int32_t d = at_texture_atlas_add(atlas, pixels, 40, 30); /* Doesn't fit    */

    CHECK(a == 0 && b == 1 && c == 2 && d == -1);
    CHECK(at_texture_atlas_get_region_count(atlas) == 3);

    at_rect ra = at_texture_atlas_get_region(atlas, 0);
    at_rect rb = at_texture_atlas_get_region(atlas, 1);
    at_rect rc = at_texture_atlas_get_region(atlas, 2);
    CHECK(same_rect(ra, at_rect_make(1.0f, 1.0f, 30.0f, 20.0f)));
    CHECK(same_rect(rb, at_rect_make(33.0f, 1.0f, 30.0f, 10.0f)));
    CHECK(same_rect(rc, at_rect_make(1.0f, 23.0f, 40.0f, 30.0f))); /* Below the tallest */

    /* Regions never overlap, even with their padding */
    CHECK(!at_rect_overlaps(ra, rb) && !at_rect_overlaps(ra, rc) && !at_rect_overlaps(rb, rc));

    /* Unknown regions are empty */
    CHECK(at_rect_is_empty(at_texture_atlas_get_region(atlas, 3)));

    /* The sprite helper fills the source (no texture before build) */
    at_sprite_desc sprite = at_texture_atlas_sprite(atlas, 1, at_vec2_zero());
    CHECK(same_rect(sprite.source, rb) && sprite.texture == NULL);

    /* Never built: no renderer needed */
    at_texture_atlas_destroy(NULL, atlas);
}

int main(void) {
    test_rect();
    test_sprite_sheet();
    test_animation();
    test_texture_atlas();

    if (s_failures > 0) {
        printf("%d of %d checks failed\n", s_failures, s_checks);
        return 1;
    }

    printf("All %d checks passed\n", s_checks);
    return 0;
}