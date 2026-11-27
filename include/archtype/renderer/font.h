#ifndef ARCHTYPE_RENDERER_FONT_H
#define ARCHTYPE_RENDERER_FONT_H

#include "archtype/core/defines.h"
#include "archtype/math/vec.h"
#include "archtype/renderer/color.h"
#include "archtype/renderer/renderer.h"
#include "archtype/renderer/renderer2d.h"
#include "archtype/renderer/texture.h"

AT_EXTERN_C_BEGIN

/*
 * Bitmap fonts and text for the 2D renderer.
 *
 * A bitmap font is an image with the characters drawn in a grid of cells
 * of the same size, in character code order, from the top-left, row by row.
 * Most bitmap fonts start at the space (code 32):
 *
 *     ! " # $ % & ' ( ) * + , - . /     <- cells 0..15  = codes 32..47
 *   0 1 2 3 4 5 6 7 8 9 : ; < = > ?     <- cells 16..31 = codes 48..63
 *   ...
 *
 * The engine has a built-in font (at_font_create_default): 5x9 pixel
 * characters in 6x10 cells, every printable ASCII character (32..126).
 *
 * Monospace fonts move every character by the cell width. Proportional
 * fonts measure each character from its pixels (the columns that are not
 * fully transparent) and put one empty texel between characters, so an
 * "i" takes less room than an "m".
 *
 * Text is UTF-8. Characters the font doesn't have are drawn as '?' when
 * the font has it, and '\n' starts a new line.
 *
 * Every character is a sprite of the font texture, so a whole text, and
 * every text in the same font, is drawn in one batch.
 */

/* Opaque font handle. */
typedef struct at_font at_font;

/* The built-in 5x9 pixel font, proportional. Returns NULL on failure. */
AT_API at_font *at_font_create_default(at_renderer *renderer);

/* Loads a grid font from an image file (PNG...). Relative paths are     */
/* resolved from the executable's folder. first_char is the code of the  */
/* top-left cell, usually 32. Every cell of the image is a character.    */
/* Returns NULL on failure.                                              */
AT_API at_font *at_font_load(at_renderer *renderer, const char *path,
                             uint32_t cell_width, uint32_t cell_height,
                             uint32_t first_char, bool monospace);

/* Same as at_font_load, from RGBA pixels in memory (4 bytes each, rows */
/* from top to bottom), e.g. a font generated in code.                 */
AT_API at_font *at_font_create_from_pixels(at_renderer *renderer, const void *pixels,
                                           uint32_t width, uint32_t height,
                                           uint32_t cell_width, uint32_t cell_height,
                                           uint32_t first_char, bool monospace);

/* Destroys the font and its texture. Passing NULL is allowed. */
AT_API void at_font_destroy(at_renderer *renderer, at_font *font);

/* Distance between two lines, in font texels (the cell height). */
AT_API float at_font_get_line_height(const at_font *font);

/* The texture with every character. */
AT_API at_texture *at_font_get_texture(const at_font *font);

/* Size of a text block in world units at the given scale: the width of */
/* its longest line (with the space after the last character) and the   */
/* height of all its lines.                                             */
AT_API at_vec2 at_font_measure(const at_font *font, const char *text, float scale);

/* ------------------------------------------------------------------------ */
/* Drawing text with the 2D renderer                                         */
/* ------------------------------------------------------------------------ */

/* Where each line goes, relative to the text position. */
typedef enum at_text_align {
    AT_TEXT_ALIGN_LEFT = 0, /* Lines start at position.x (default) */
    AT_TEXT_ALIGN_CENTER,   /* Lines are centered on position.x    */
    AT_TEXT_ALIGN_RIGHT     /* Lines end at position.x             */
} at_text_align;

/* Everything about one text. Start from at_text_desc_make. */
typedef struct at_text_desc {
    const at_font *font;
    const char    *text;     /* UTF-8; '\n' starts a new line              */
    at_vec2        position; /* Top of the first line, in world units: its */
                             /* left end, center or right end (see align)  */
    float          scale;    /* World units per font texel: 2 = twice as   */
                             /* big. Whole numbers keep pixel fonts sharp  */
    at_color       color;
    at_text_align  align;
} at_text_desc;

/* Text at position, scale 1, white, aligned left. */
static inline at_text_desc at_text_desc_make(const at_font *font, const char *text,
                                             at_vec2 position) {
    at_text_desc desc;
    desc.font     = font;
    desc.text     = text;
    desc.position = position;
    desc.scale    = 1.0f;
    desc.color    = at_color_white();
    desc.align    = AT_TEXT_ALIGN_LEFT;
    return desc;
}

/* Draws text with its top-left corner at position. Only valid between */
/* at_renderer2d_begin and at_renderer2d_end, like every 2D drawing,   */
/* and it uses the current layer and blend mode.                       */
AT_API void at_renderer2d_draw_text(at_renderer2d *renderer2d, const at_font *font,
                                    const char *text, at_vec2 position, float scale,
                                    at_color color);

/* Draws text with every option (see at_text_desc). */
AT_API void at_renderer2d_draw_text_ex(at_renderer2d *renderer2d, const at_text_desc *desc);

AT_EXTERN_C_END

#endif /* ARCHTYPE_RENDERER_FONT_H */