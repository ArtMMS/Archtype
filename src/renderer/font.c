#include "archtype/renderer/font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "renderer/image_loader.h"

#define AT_FONT_BYTES_PER_PIXEL 4
#define AT_FONT_TAB_SPACES      4

/* ------------------------------------------------------------------------ */
/* Built-in font                                                             */
/* ------------------------------------------------------------------------ */

/* Archtype's own 5x9 pixel font, ASCII 32..126. Each character is 9 rows */
/* from top to bottom; in each row, bit 4 is the leftmost pixel. Rows 0-6 */
/* are above the baseline, rows 7-8 hold the descenders (g, j, p, q, y).  */
#define AT_DEFAULT_FONT_FIRST   32
#define AT_DEFAULT_FONT_COUNT   95
#define AT_DEFAULT_FONT_WIDTH   5
#define AT_DEFAULT_FONT_HEIGHT  9
#define AT_DEFAULT_FONT_CELL_W  6  /* One empty column and row around each */
#define AT_DEFAULT_FONT_CELL_H  10 /* character: spacing and padding        */
#define AT_DEFAULT_FONT_COLUMNS 16

static const uint8_t k_default_font[AT_DEFAULT_FONT_COUNT][AT_DEFAULT_FONT_HEIGHT] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /*   */
    { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04, 0x00, 0x00 }, /* ! */
    { 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* " */
    { 0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A, 0x00, 0x00 }, /* # */
    { 0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04, 0x00, 0x00 }, /* $ */
    { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03, 0x00, 0x00 }, /* % */
    { 0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D, 0x00, 0x00 }, /* & */
    { 0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* quote */
    { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02, 0x00, 0x00 }, /* ( */
    { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08, 0x00, 0x00 }, /* ) */
    { 0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00, 0x00, 0x00 }, /* * */
    { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00, 0x00, 0x00 }, /* + */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x02, 0x04 }, /* , */
    { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* - */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00 }, /* . */
    { 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00, 0x00 }, /* / */
    { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E, 0x00, 0x00 }, /* 0 */
    { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00, 0x00 }, /* 1 */
    { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F, 0x00, 0x00 }, /* 2 */
    { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E, 0x00, 0x00 }, /* 3 */
    { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02, 0x00, 0x00 }, /* 4 */
    { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E, 0x00, 0x00 }, /* 5 */
    { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E, 0x00, 0x00 }, /* 6 */
    { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08, 0x00, 0x00 }, /* 7 */
    { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E, 0x00, 0x00 }, /* 8 */
    { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C, 0x00, 0x00 }, /* 9 */
    { 0x00, 0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00 }, /* : */
    { 0x00, 0x00, 0x04, 0x00, 0x00, 0x04, 0x04, 0x08, 0x00 }, /* ; */
    { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02, 0x00, 0x00 }, /* < */
    { 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x00 }, /* = */
    { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08, 0x00, 0x00 }, /* > */
    { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04, 0x00, 0x00 }, /* ? */
    { 0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E, 0x00, 0x00 }, /* @ */
    { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x00, 0x00 }, /* A */
    { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E, 0x00, 0x00 }, /* B */
    { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E, 0x00, 0x00 }, /* C */
    { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C, 0x00, 0x00 }, /* D */
    { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F, 0x00, 0x00 }, /* E */
    { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10, 0x00, 0x00 }, /* F */
    { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F, 0x00, 0x00 }, /* G */
    { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x00, 0x00 }, /* H */
    { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00, 0x00 }, /* I */
    { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C, 0x00, 0x00 }, /* J */
    { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11, 0x00, 0x00 }, /* K */
    { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F, 0x00, 0x00 }, /* L */
    { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11, 0x00, 0x00 }, /* M */
    { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x00, 0x00 }, /* N */
    { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00, 0x00 }, /* O */
    { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10, 0x00, 0x00 }, /* P */
    { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D, 0x00, 0x00 }, /* Q */
    { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11, 0x00, 0x00 }, /* R */
    { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E, 0x00, 0x00 }, /* S */
    { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x00 }, /* T */
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00, 0x00 }, /* U */
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04, 0x00, 0x00 }, /* V */
    { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A, 0x00, 0x00 }, /* W */
    { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11, 0x00, 0x00 }, /* X */
    { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x00, 0x00 }, /* Y */
    { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F, 0x00, 0x00 }, /* Z */
    { 0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E, 0x00, 0x00 }, /* [ */
    { 0x00, 0x10, 0x08, 0x04, 0x02, 0x01, 0x00, 0x00, 0x00 }, /* backslash */
    { 0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E, 0x00, 0x00 }, /* ] */
    { 0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* ^ */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x00 }, /* _ */
    { 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* ` */
    { 0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F, 0x00, 0x00 }, /* a */
    { 0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x1E, 0x00, 0x00 }, /* b */
    { 0x00, 0x00, 0x0E, 0x10, 0x10, 0x11, 0x0E, 0x00, 0x00 }, /* c */
    { 0x01, 0x01, 0x0F, 0x11, 0x11, 0x11, 0x0F, 0x00, 0x00 }, /* d */
    { 0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E, 0x00, 0x00 }, /* e */
    { 0x06, 0x08, 0x1E, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00 }, /* f */
    { 0x00, 0x00, 0x0F, 0x11, 0x11, 0x11, 0x0F, 0x01, 0x0E }, /* g */
    { 0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x11, 0x00, 0x00 }, /* h */
    { 0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E, 0x00, 0x00 }, /* i */
    { 0x02, 0x00, 0x06, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C }, /* j */
    { 0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12, 0x00, 0x00 }, /* k */
    { 0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00, 0x00 }, /* l */
    { 0x00, 0x00, 0x1A, 0x15, 0x15, 0x15, 0x15, 0x00, 0x00 }, /* m */
    { 0x00, 0x00, 0x1E, 0x11, 0x11, 0x11, 0x11, 0x00, 0x00 }, /* n */
    { 0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E, 0x00, 0x00 }, /* o */
    { 0x00, 0x00, 0x1E, 0x11, 0x11, 0x11, 0x1E, 0x10, 0x10 }, /* p */
    { 0x00, 0x00, 0x0F, 0x11, 0x11, 0x11, 0x0F, 0x01, 0x01 }, /* q */
    { 0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10, 0x00, 0x00 }, /* r */
    { 0x00, 0x00, 0x0F, 0x10, 0x0E, 0x01, 0x1E, 0x00, 0x00 }, /* s */
    { 0x08, 0x08, 0x1E, 0x08, 0x08, 0x09, 0x06, 0x00, 0x00 }, /* t */
    { 0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D, 0x00, 0x00 }, /* u */
    { 0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04, 0x00, 0x00 }, /* v */
    { 0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A, 0x00, 0x00 }, /* w */
    { 0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x00, 0x00 }, /* x */
    { 0x00, 0x00, 0x11, 0x11, 0x11, 0x11, 0x0F, 0x01, 0x0E }, /* y */
    { 0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F, 0x00, 0x00 }, /* z */
    { 0x03, 0x04, 0x04, 0x08, 0x04, 0x04, 0x03, 0x00, 0x00 }, /* { */
    { 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00 }, /* | */
    { 0x18, 0x04, 0x04, 0x02, 0x04, 0x04, 0x18, 0x00, 0x00 }, /* } */
    { 0x00, 0x00, 0x08, 0x15, 0x02, 0x00, 0x00, 0x00, 0x00 }, /* ~ */
};

/* ------------------------------------------------------------------------ */
/* Font                                                                      */
/* ------------------------------------------------------------------------ */

typedef struct at_glyph {
    at_rect source;  /* Texels of the character in the texture; empty = nothing to draw */
    float   advance; /* How far the next character starts, in texels                    */
} at_glyph;

struct at_font {
    at_texture *texture;
    uint32_t    first_char;
    uint32_t    glyph_count;
    at_glyph   *glyphs;
    float       line_height;
    float       space_advance;
    int32_t     fallback;    /* Glyph drawn for unknown characters ('?'), or -1 */
};

/* True if any texel of the column x in the cell is not fully transparent */
static bool column_has_ink(const uint8_t *pixels, uint32_t width, uint32_t x,
                           uint32_t top, uint32_t height) {
    for (uint32_t y = top; y < top + height; ++y) {
        if (pixels[((size_t)y * width + x) * AT_FONT_BYTES_PER_PIXEL + 3] != 0) {
            return true;
        }
    }
    return false;
}

/* Fills the glyphs from the grid. Proportional: each character is as wide */
/* as its ink, plus one texel of space; empty cells (the space) advance by */
/* half a cell.                                                           */
static void measure_glyphs(at_font *font, const uint8_t *pixels, uint32_t width,
                           uint32_t cell_width, uint32_t cell_height, uint32_t columns,
                           bool monospace) {
    for (uint32_t i = 0; i < font->glyph_count; ++i) {
        uint32_t  cell_x = (i % columns) * cell_width;
        uint32_t  cell_y = (i / columns) * cell_height;
        at_glyph *glyph  = &font->glyphs[i];

        if (monospace) {
            glyph->source  = at_rect_make((float)cell_x, (float)cell_y,
                                          (float)cell_width, (float)cell_height);
            glyph->advance = (float)cell_width;
            continue;
        }

        uint32_t left  = cell_width;
        uint32_t right = 0;
        for (uint32_t x = 0; x < cell_width; ++x) {
            if (column_has_ink(pixels, width, cell_x + x, cell_y, cell_height)) {
                if (x < left) {
                    left = x;
                }
                right = x;
            }
        }

        if (left > right) {
            glyph->source  = at_rect_make(0.0f, 0.0f, 0.0f, 0.0f);
            glyph->advance = font->space_advance;
        } else {
            uint32_t ink   = right - left + 1;
            glyph->source  = at_rect_make((float)(cell_x + left), (float)cell_y,
                                          (float)ink, (float)cell_height);
            glyph->advance = (float)(ink + 1);
        }
    }
}

at_font *at_font_create_from_pixels(at_renderer *renderer, const void *pixels,
                                    uint32_t width, uint32_t height,
                                    uint32_t cell_width, uint32_t cell_height,
                                    uint32_t first_char, bool monospace) {
    if (!renderer || !pixels || cell_width == 0 || cell_height == 0 ||
        width < cell_width || height < cell_height) {
        fprintf(stderr, "[archtype] Invalid font: the image must hold at least one "
                        "cell\n");
        return NULL;
    }

    uint32_t columns = width / cell_width;
    uint32_t rows    = height / cell_height;

    at_font *font = (at_font *)calloc(1, sizeof(*font));
    if (!font) {
        fprintf(stderr, "[archtype] Failed to allocate font\n");
        return NULL;
    }

    font->first_char    = first_char;
    font->glyph_count   = columns * rows;
    font->line_height   = (float)cell_height;
    font->space_advance = monospace ? (float)cell_width
                                    : (float)(cell_width / 2 > 0 ? cell_width / 2 : 1);
    font->glyphs        = (at_glyph *)calloc(font->glyph_count, sizeof(at_glyph));
    if (!font->glyphs) {
        fprintf(stderr, "[archtype] Failed to allocate font\n");
        free(font);
        return NULL;
    }

    measure_glyphs(font, (const uint8_t *)pixels, width, cell_width, cell_height, columns,
                   monospace);

    font->fallback = -1;
    if ('?' >= first_char && '?' - first_char < font->glyph_count) {
        font->fallback = (int32_t)('?' - first_char);
    }

    /* Nearest and clamped: pixel fonts stay sharp at whole-number scales */
    at_texture_options options;
    memset(&options, 0, sizeof(options));
    options.filter = AT_TEXTURE_FILTER_NEAREST;
    options.wrap   = AT_TEXTURE_WRAP_CLAMP;

    font->texture = at_texture_create(renderer, width, height, pixels, &options);
    if (!font->texture) {
        free(font->glyphs);
        free(font);
        return NULL;
    }
    return font;
}

at_font *at_font_create_default(at_renderer *renderer) {
    const uint32_t rows   = (AT_DEFAULT_FONT_COUNT + AT_DEFAULT_FONT_COLUMNS - 1)
                          / AT_DEFAULT_FONT_COLUMNS;
    const uint32_t width  = AT_DEFAULT_FONT_COLUMNS * AT_DEFAULT_FONT_CELL_W;
    const uint32_t height = rows * AT_DEFAULT_FONT_CELL_H;

    uint8_t *pixels = (uint8_t *)calloc((size_t)width * height, AT_FONT_BYTES_PER_PIXEL);
    if (!pixels) {
        fprintf(stderr, "[archtype] Out of memory creating the default font\n");
        return NULL;
    }

    /* White ink; the text color comes from the sprite tint. Transparent */
    /* texels stay white too, so filtering never darkens the edges.      */
    for (size_t i = 0; i < (size_t)width * height; ++i) {
        pixels[i * AT_FONT_BYTES_PER_PIXEL + 0] = 255;
        pixels[i * AT_FONT_BYTES_PER_PIXEL + 1] = 255;
        pixels[i * AT_FONT_BYTES_PER_PIXEL + 2] = 255;
    }

    for (uint32_t c = 0; c < AT_DEFAULT_FONT_COUNT; ++c) {
        uint32_t cell_x = (c % AT_DEFAULT_FONT_COLUMNS) * AT_DEFAULT_FONT_CELL_W;
        uint32_t cell_y = (c / AT_DEFAULT_FONT_COLUMNS) * AT_DEFAULT_FONT_CELL_H;

        for (uint32_t y = 0; y < AT_DEFAULT_FONT_HEIGHT; ++y) {
            for (uint32_t x = 0; x < AT_DEFAULT_FONT_WIDTH; ++x) {
                if (k_default_font[c][y] & (1u << (AT_DEFAULT_FONT_WIDTH - 1 - x))) {
                    size_t texel = (size_t)(cell_y + y) * width + cell_x + x;
                    pixels[texel * AT_FONT_BYTES_PER_PIXEL + 3] = 255;
                }
            }
        }
    }

    at_font *font = at_font_create_from_pixels(renderer, pixels, width, height,
                                               AT_DEFAULT_FONT_CELL_W, AT_DEFAULT_FONT_CELL_H,
                                               AT_DEFAULT_FONT_FIRST, false);
    free(pixels);
    return font;
}

at_font *at_font_load(at_renderer *renderer, const char *path,
                      uint32_t cell_width, uint32_t cell_height,
                      uint32_t first_char, bool monospace) {
    if (!renderer || !path) {
        return NULL;
    }

    at_image_data image;
    if (!at_image_load(path, &image)) {
        return NULL;
    }

    at_font *font = at_font_create_from_pixels(renderer, image.pixels, image.width,
                                               image.height, cell_width, cell_height,
                                               first_char, monospace);
    at_image_free(&image);
    return font;
}

void at_font_destroy(at_font *font, at_renderer *renderer) {
    if (!font) {
        return;
    }

    if (renderer) {
        at_texture_destroy(renderer, font->texture);
    }
    free(font->glyphs);
    free(font);
}

float at_font_get_line_height(const at_font *font) {
    return font ? font->line_height : 0.0f;
}

at_texture *at_font_get_texture(const at_font *font) {
    return font ? font->texture : NULL;
}

/* ------------------------------------------------------------------------ */
/* Text                                                                      */
/* ------------------------------------------------------------------------ */

/* Reads one UTF-8 character and moves *text past it. Broken sequences */
/* give U+FFFD, which no bitmap font has, so it is drawn as '?'.       */
static uint32_t next_codepoint(const char **text) {
    const uint8_t *s = (const uint8_t *)*text;
    uint32_t       c = s[0];
    int            extra;

    if (c < 0x80) {
        *text += 1;
        return c;
    } else if ((c & 0xE0) == 0xC0) {
        c &= 0x1F;
        extra = 1;
    } else if ((c & 0xF0) == 0xE0) {
        c &= 0x0F;
        extra = 2;
    } else if ((c & 0xF8) == 0xF0) {
        c &= 0x07;
        extra = 3;
    } else {
        *text += 1;
        return 0xFFFD;
    }

    for (int i = 1; i <= extra; ++i) {
        if ((s[i] & 0xC0) != 0x80) {
            *text += i; /* Stop at the byte that broke the sequence */
            return 0xFFFD;
        }
        c = (c << 6) | (s[i] & 0x3F);
    }

    *text += 1 + extra;
    return c;
}

/* Glyph of a character, the fallback if the font doesn't have it, or NULL */
static const at_glyph *find_glyph(const at_font *font, uint32_t codepoint) {
    if (codepoint >= font->first_char && codepoint - font->first_char < font->glyph_count) {
        return &font->glyphs[codepoint - font->first_char];
    }
    return font->fallback >= 0 ? &font->glyphs[font->fallback] : NULL;
}

/* How far a character moves the pen, in texels */
static float advance_of(const at_font *font, uint32_t codepoint, const at_glyph *glyph) {
    if (codepoint == '\t') {
        return font->space_advance * AT_FONT_TAB_SPACES;
    }
    if (codepoint == ' ') {
        return font->space_advance;
    }
    return glyph ? glyph->advance : font->space_advance;
}

/* Width in texels of the line that starts at text; *end is set to its */
/* '\n' or to the end of the text.                                     */
static float line_width(const at_font *font, const char *text, const char **end) {
    float width = 0.0f;
    while (*text != '\0' && *text != '\n') {
        uint32_t codepoint = next_codepoint(&text);
        width += advance_of(font, codepoint, find_glyph(font, codepoint));
    }
    *end = text;
    return width;
}

at_vec2 at_font_measure(const at_font *font, const char *text, float scale) {
    if (!font || !text) {
        return at_vec2_zero();
    }

    float       widest = 0.0f;
    uint32_t    lines  = 1;
    const char *end    = text;

    for (;;) {
        float width = line_width(font, text, &end);
        if (width > widest) {
            widest = width;
        }
        if (*end == '\0') {
            break;
        }
        text = end + 1;
        lines++;
    }

    return at_vec2_make(widest * scale, (float)lines * font->line_height * scale);
}

void at_renderer2d_draw_text(at_renderer2d *renderer2d, const at_font *font, const char *text,
                             at_vec2 position, float scale, at_color color) {
    at_text_desc desc = at_text_desc_make(font, text, position);
    desc.scale = scale;
    desc.color = color;
    at_renderer2d_draw_text_ex(renderer2d, &desc);
}

void at_renderer2d_draw_text_ex(at_renderer2d *renderer2d, const at_text_desc *desc) {
    if (!renderer2d || !desc || !desc->font || !desc->text) {
        return;
    }

    const at_font *font  = desc->font;
    float          scale = desc->scale > 0.0f ? desc->scale : 1.0f;
    const char    *line  = desc->text;
    float          top   = desc->position.y;

    /* Every character is a sprite of the font texture, placed by its */
    /* top-left corner: +Y is up, so lines go down from the position. */
    at_sprite_desc sprite = at_sprite_desc_make(font->texture, at_vec2_zero());
    sprite.origin = at_vec2_make(0.0f, 1.0f);
    sprite.tint   = desc->color;

    for (;;) {
        const char *end   = line;
        float       width = line_width(font, line, &end) * scale;

        float x = desc->position.x;
        if (desc->align == AT_TEXT_ALIGN_CENTER) {
            x -= width * 0.5f;
        } else if (desc->align == AT_TEXT_ALIGN_RIGHT) {
            x -= width;
        }

        const char *cursor = line;
        while (cursor < end) {
            uint32_t        codepoint = next_codepoint(&cursor);
            const at_glyph *glyph     = codepoint == ' ' || codepoint == '\t'
                                      ? NULL : find_glyph(font, codepoint);

            if (glyph && !at_rect_is_empty(glyph->source)) {
                sprite.source   = glyph->source;
                sprite.position = at_vec2_make(x, top);
                sprite.size     = at_vec2_make(glyph->source.width * scale,
                                               glyph->source.height * scale);
                at_renderer2d_draw_sprite_ex(renderer2d, &sprite);
            }
            x += advance_of(font, codepoint, glyph) * scale;
        }

        if (*end == '\0') {
            break;
        }
        line = end + 1;
        top -= font->line_height * scale;
    }
}