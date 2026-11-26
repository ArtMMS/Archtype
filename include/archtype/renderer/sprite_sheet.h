#ifndef ARCHTYPE_RENDERER_SPRITE_SHEET_H
#define ARCHTYPE_RENDERER_SPRITE_SHEET_H

#include "archtype/core/defines.h"
#include "archtype/math/rect.h"
#include "archtype/renderer/renderer2d.h"
#include "archtype/renderer/texture.h"

AT_EXTERN_C_BEGIN

/*
 * Sprite sheet: a texture cut into a grid of frames of the same size, e.g.
 * the frames of an animation or the tiles of a tileset.
 *
 *   margin               spacing
 *   v                    v
 *   +--------------------------------+
 *   |  [ 0 ] [ 1 ] [ 2 ] [ 3 ]       |   Frames are numbered from the
 *   |  [ 4 ] [ 5 ] [ 6 ] [ 7 ]       |   top-left, row by row.
 *   +--------------------------------+
 *
 * margin:  empty texels around the whole grid
 * spacing: empty texels between two frames
 */
typedef struct at_sprite_sheet {
    at_texture *texture;
    uint32_t    frame_width;  /* Texels */
    uint32_t    frame_height;
    uint32_t    columns;
    uint32_t    rows;
    uint32_t    margin;
    uint32_t    spacing;
} at_sprite_sheet;

/* Sheet for a texture whose size is already known (e.g. a sheet inside a */
/* bigger image, or for tests). Fits as many whole frames as possible.    */
static inline at_sprite_sheet at_sprite_sheet_make_sized(at_texture *texture,
                                                         uint32_t texture_width,
                                                         uint32_t texture_height,
                                                         uint32_t frame_width,
                                                         uint32_t frame_height,
                                                         uint32_t margin, uint32_t spacing) {
    at_sprite_sheet sheet;
    sheet.texture      = texture;
    sheet.frame_width  = frame_width;
    sheet.frame_height = frame_height;
    sheet.margin       = margin;
    sheet.spacing      = spacing;
    sheet.columns      = 0;
    sheet.rows         = 0;

    /* n frames take n * frame + (n - 1) * spacing texels */
    if (frame_width > 0 && texture_width >= 2 * margin + frame_width) {
        sheet.columns = (texture_width - 2 * margin + spacing) / (frame_width + spacing);
    }
    if (frame_height > 0 && texture_height >= 2 * margin + frame_height) {
        sheet.rows = (texture_height - 2 * margin + spacing) / (frame_height + spacing);
    }
    return sheet;
}

/* Sheet that cuts the whole texture into frame_width x frame_height frames. */
/* margin and spacing are 0 for most sheets.                                 */
static inline at_sprite_sheet at_sprite_sheet_make(at_texture *texture,
                                                   uint32_t frame_width, uint32_t frame_height,
                                                   uint32_t margin, uint32_t spacing) {
    uint32_t width  = 0;
    uint32_t height = 0;
    if (texture) {
        at_texture_get_size(texture, &width, &height);
    }
    return at_sprite_sheet_make_sized(texture, width, height, frame_width, frame_height,
                                      margin, spacing);
}

static inline uint32_t at_sprite_sheet_frame_count(const at_sprite_sheet *sheet) {
    return sheet->columns * sheet->rows;
}

/* Rectangle of a frame in texels, ready for at_sprite_desc.source.  */
/* Frames past the last one wrap around, so animations can just keep */
/* counting up.                                                      */
static inline at_rect at_sprite_sheet_frame(const at_sprite_sheet *sheet, uint32_t frame) {
    uint32_t count = at_sprite_sheet_frame_count(sheet);
    if (count == 0) {
        return at_rect_make(0.0f, 0.0f, 0.0f, 0.0f);
    }

    frame %= count;
    uint32_t column = frame % sheet->columns;
    uint32_t row    = frame / sheet->columns;

    return at_rect_make((float)(sheet->margin + column * (sheet->frame_width + sheet->spacing)),
                        (float)(sheet->margin + row * (sheet->frame_height + sheet->spacing)),
                        (float)sheet->frame_width, (float)sheet->frame_height);
}

/* A sprite of one frame at position, with the defaults of at_sprite_desc_make. */
static inline at_sprite_desc at_sprite_sheet_sprite(const at_sprite_sheet *sheet, uint32_t frame,
                                                    at_vec2 position) {
    at_sprite_desc sprite = at_sprite_desc_make(sheet->texture, position);
    sprite.source = at_sprite_sheet_frame(sheet, frame);
    return sprite;
}

/* ------------------------------------------------------------------------ */
/* Animation                                                                 */
/* ------------------------------------------------------------------------ */

/* A run of frames of a sprite sheet played at a fixed speed. */
typedef struct at_animation {
    uint32_t first_frame;
    uint32_t frame_count;
    float    fps;  /* Frames per second */
    bool     loop; /* false: stops on the last frame */
} at_animation;

static inline at_animation at_animation_make(uint32_t first_frame, uint32_t frame_count,
                                             float fps, bool loop) {
    at_animation animation;
    animation.first_frame = first_frame;
    animation.frame_count = frame_count;
    animation.fps         = fps;
    animation.loop        = loop;
    return animation;
}

/* Sheet frame to show `time` seconds after the animation started. */
static inline uint32_t at_animation_frame(const at_animation *animation, float time) {
    if (animation->frame_count == 0 || animation->fps <= 0.0f || time <= 0.0f) {
        return animation->first_frame;
    }

    uint32_t step = (uint32_t)(time * animation->fps);
    if (animation->loop) {
        step %= animation->frame_count;
    } else if (step >= animation->frame_count) {
        step = animation->frame_count - 1;
    }
    return animation->first_frame + step;
}

/* True once an animation that doesn't loop reached its last frame's end. */
static inline bool at_animation_is_finished(const at_animation *animation, float time) {
    if (animation->loop || animation->fps <= 0.0f) {
        return false;
    }
    return time * animation->fps >= (float)animation->frame_count;
}

/* Seconds one pass through all the frames takes. */
static inline float at_animation_duration(const at_animation *animation) {
    return animation->fps > 0.0f ? (float)animation->frame_count / animation->fps : 0.0f;
}

AT_EXTERN_C_END

#endif /* ARCHTYPE_RENDERER_SPRITE_SHEET_H */