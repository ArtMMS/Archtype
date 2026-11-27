/*
 * Wavefront OBJ and MTL loader (see model.h for what is supported).
 *
 * Numbers are parsed here instead of with strtof/atof: those follow the C
 * locale, and with a locale that writes decimals with a comma (as in
 * Portuguese) "0.5" would not be read correctly.
 */

#include "archtype/renderer/model.h"
#include "archtype/renderer/mesh_utils.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/file.h"

#define OBJ_MISSING (-1) /* A corner without a UV or a normal */

/* ------------------------------------------------------------------------ */
/* Parser state                                                              */
/* ------------------------------------------------------------------------ */

/* One corner of a face: indices into the file's lists, from 0 */
typedef struct obj_corner {
    int32_t position;
    int32_t uv;     /* OBJ_MISSING if none */
    int32_t normal; /* OBJ_MISSING if none */
} obj_corner;

/* A part being built */
typedef struct obj_part {
    char         name[AT_MODEL_NAME_MAX];
    char         material[AT_MODEL_NAME_MAX];

    at_vertex3d *vertices;
    uint32_t     vertex_count;
    uint32_t     vertex_capacity;
    obj_corner  *keys;          /* The corner each vertex came from */
    uint32_t     key_capacity;

    uint32_t    *indices;
    uint32_t     index_count;
    uint32_t     index_capacity;

    uint32_t    *table;         /* Corner -> vertex, open addressing */
    uint32_t     table_capacity;

    bool         missing_normals;
} obj_part;

typedef struct obj_parser {
    at_model_data *data;
    uint32_t       material_capacity;

    const char    *source; /* File name, for messages */
    uint32_t       line;

    float         *positions; /* 3 per position */
    uint32_t       position_count;
    uint32_t       position_capacity;
    float         *uvs;       /* 2 per UV */
    uint32_t       uv_count;
    uint32_t       uv_capacity;
    float         *normals;   /* 3 per normal */
    uint32_t       normal_count;
    uint32_t       normal_capacity;

    obj_part      *parts;
    uint32_t       part_count;
    uint32_t       part_capacity;

    /* State set by o, g and usemtl for the faces that follow */
    char           name[AT_MODEL_NAME_MAX];
    char           material[AT_MODEL_NAME_MAX];
    bool           new_part;

    obj_corner    *face; /* Corners of the face being read */
    uint32_t       face_capacity;

    const char    *folder; /* For mtllib; NULL: ignore mtllib */
} obj_parser;

/* A line of text, without its line break */
typedef struct text_line {
    const char *cursor;
    const char *end;
} text_line;

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

static void parse_error(const obj_parser *parser, const char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "[archtype] OBJ %s:%u: ", parser->source, (unsigned)parser->line);
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
    va_end(args);
}

/* Makes room for needed elements; false if out of memory */
static bool reserve(void **items, uint32_t *capacity, uint32_t needed, size_t item_size) {
    if (needed <= *capacity) {
        return true;
    }
    if (needed > 0x7FFFFFFFu) {
        return false;
    }

    uint32_t new_capacity = *capacity ? *capacity : 16;
    while (new_capacity < needed) {
        new_capacity = new_capacity > 0x3FFFFFFFu ? needed : new_capacity * 2;
    }

    void *grown = realloc(*items, (size_t)new_capacity * item_size);
    if (!grown) {
        return false;
    }
    *items    = grown;
    *capacity = new_capacity;
    return true;
}

static void copy_name(char *out, size_t out_size, const char *text, size_t length) {
    if (length >= out_size) {
        length = out_size - 1;
    }
    memcpy(out, text, length);
    out[length] = '\0';
}

static bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\v' || c == '\f';
}

static void skip_spaces(text_line *line) {
    while (line->cursor < line->end && is_space(*line->cursor)) {
        line->cursor++;
    }
}

static bool at_line_end(text_line *line) {
    skip_spaces(line);
    return line->cursor >= line->end;
}

/* Takes the next word; false if there is none */
static bool next_word(text_line *line, const char **word, size_t *length) {
    skip_spaces(line);
    if (line->cursor >= line->end) {
        return false;
    }
    *word = line->cursor;
    while (line->cursor < line->end && !is_space(*line->cursor)) {
        line->cursor++;
    }
    *length = (size_t)(line->cursor - *word);
    return true;
}

/* The rest of the line, without spaces around it (may be empty) */
static void rest_of_line(text_line *line, const char **text, size_t *length) {
    skip_spaces(line);
    const char *end = line->end;
    while (end > line->cursor && is_space(end[-1])) {
        end--;
    }
    *text        = line->cursor;
    *length      = (size_t)(end - line->cursor);
    line->cursor = line->end;
}

static bool word_is(const char *word, size_t length, const char *keyword) {
    return strlen(keyword) == length && memcmp(word, keyword, length) == 0;
}

/* Splits the next line off text; comments (# at the start or after a */
/* space) are cut. False at the end of the text.                       */
static bool next_line(const char **text, const char *text_end, text_line *line) {
    if (*text >= text_end) {
        return false;
    }

    const char *start = *text;
    const char *end   = start;
    while (end < text_end && *end != '\n') {
        end++;
    }
    *text = end < text_end ? end + 1 : end;

    if (end > start && end[-1] == '\r') {
        end--;
    }
    for (const char *c = start; c < end; ++c) {
        if (*c == '#' && (c == start || is_space(c[-1]))) {
            end = c;
            break;
        }
    }

    line->cursor = start;
    line->end    = end;
    return true;
}

/* A number like 12, -0.5, .25, 1e-3 or 2.5E+2, followed by a space or */
/* the end of the line. Locale independent.                            */
static bool parse_float(text_line *line, float *out) {
    skip_spaces(line);
    const char *c   = line->cursor;
    const char *end = line->end;

    double sign = 1.0;
    if (c < end && (*c == '-' || *c == '+')) {
        sign = *c == '-' ? -1.0 : 1.0;
        c++;
    }

    double value  = 0.0;
    int    digits = 0;
    int    scale  = 0; /* Power of ten for the digits after the point */
    while (c < end && *c >= '0' && *c <= '9') {
        value = value * 10.0 + (double)(*c - '0');
        c++;
        digits++;
    }
    if (c < end && *c == '.') {
        c++;
        while (c < end && *c >= '0' && *c <= '9') {
            value = value * 10.0 + (double)(*c - '0');
            scale--;
            c++;
            digits++;
        }
    }
    if (digits == 0) {
        return false;
    }

    if (c < end && (*c == 'e' || *c == 'E')) {
        c++;
        int exponent_sign = 1;
        if (c < end && (*c == '-' || *c == '+')) {
            exponent_sign = *c == '-' ? -1 : 1;
            c++;
        }
        int exponent        = 0;
        int exponent_digits = 0;
        while (c < end && *c >= '0' && *c <= '9') {
            if (exponent < 10000) {
                exponent = exponent * 10 + (*c - '0');
            }
            c++;
            exponent_digits++;
        }
        if (exponent_digits == 0) {
            return false;
        }
        scale += exponent_sign * exponent;
    }

    if (c < end && !is_space(*c)) {
        return false;
    }

    if (scale != 0) {
        value *= pow(10.0, (double)scale);
    }
    *out         = (float)(sign * value);
    line->cursor = c;
    return true;
}

/* A whole number (face indices); stops at '/', a space or the end */
static bool parse_index(const char **cursor, const char *end, long *out) {
    const char *c        = *cursor;
    bool        negative = false;
    if (c < end && (*c == '-' || *c == '+')) {
        negative = *c == '-';
        c++;
    }

    long value  = 0;
    int  digits = 0;
    while (c < end && *c >= '0' && *c <= '9') {
        /* Anything this big is out of range anyway: stop growing */
        if (value < 100000000L) {
            value = value * 10 + (*c - '0');
        }
        c++;
        digits++;
    }
    if (digits == 0) {
        return false;
    }

    *out    = negative ? -value : value;
    *cursor = c;
    return true;
}

/* Reads up to max floats; returns how many were read, -1 on a bad number */
static int parse_floats(text_line *line, float *out, int max) {
    int count = 0;
    while (count < max && !at_line_end(line)) {
        if (!parse_float(line, &out[count])) {
            return -1;
        }
        count++;
    }
    return count;
}

/* Path helpers: folder of a path (with its last slash), absolute paths */
static size_t folder_length(const char *path) {
    size_t length = 0;
    for (size_t i = 0; path[i]; ++i) {
        if (path[i] == '/' || path[i] == '\\') {
            length = i + 1;
        }
    }
    return length;
}

static bool is_absolute(const char *path) {
    if (path[0] == '/' || path[0] == '\\') {
        return true;
    }
    return ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'))
        && path[1] == ':';
}

/* folder + path into out, with '\' turned into '/' (which every system */
/* accepts). false if it doesn't fit.                                    */
static bool join_path(char *out, size_t out_size, const char *folder, const char *path,
                      size_t path_length) {
    char relative[AT_MODEL_PATH_MAX];
    if (path_length >= sizeof(relative)) {
        return false;
    }
    memcpy(relative, path, path_length);
    relative[path_length] = '\0';

    size_t used = 0;
    if (folder && !is_absolute(relative)) {
        used = strlen(folder);
        if (used >= out_size) {
            return false;
        }
        memcpy(out, folder, used);
    }
    if (used + path_length >= out_size) {
        return false;
    }
    memcpy(out + used, relative, path_length + 1);

    for (char *c = out; *c; ++c) {
        if (*c == '\\') {
            *c = '/';
        }
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* MTL                                                                       */
/* ------------------------------------------------------------------------ */

static float linear_to_srgb(float c) {
    c = at_clampf(c, 0.0f, 1.0f);
    if (c <= 0.0031308f) {
        return c * 12.92f;
    }
    return 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

/* "r g b" or a single value for all three */
static bool parse_rgb(text_line *line, float rgb[3]) {
    int count = parse_floats(line, rgb, 3);
    if (count == 1) {
        rgb[1] = rgb[0];
        rgb[2] = rgb[0];
        return true;
    }
    return count == 3;
}

static bool is_number_word(const char *word, size_t length) {
    text_line probe = { word, word + length };
    float     value;
    return parse_float(&probe, &value) && at_line_end(&probe);
}

/* The file of a map_* line, after its options (-s 1 1 1, -bm 0.5, -clamp */
/* on...), relative to the MTL's folder                                   */
static void parse_texture_path(const obj_parser *parser, text_line *line, const char *folder,
                               char *out) {
    for (;;) {
        skip_spaces(line);
        if (line->cursor + 1 >= line->end || line->cursor[0] != '-' ||
            is_space(line->cursor[1])) {
            break;
        }

        const char *word;
        size_t      length;
        next_word(line, &word, &length); /* The option's name */

        /* Its values: numbers, or on / off */
        for (;;) {
            text_line   peek = *line;
            const char *value;
            size_t      value_length;
            if (!next_word(&peek, &value, &value_length)) {
                break;
            }
            if (!is_number_word(value, value_length) && !word_is(value, value_length, "on") &&
                !word_is(value, value_length, "off")) {
                break;
            }
            *line = peek;
        }
    }

    const char *path;
    size_t      length;
    rest_of_line(line, &path, &length);
    if (length == 0) {
        return;
    }
    if (!join_path(out, AT_MODEL_PATH_MAX, folder, path, length)) {
        parse_error(parser, "texture path is too long, skipped");
        out[0] = '\0';
    }
}

static at_model_material_data *add_material(obj_parser *parser, const char *name,
                                            size_t length) {
    at_model_data *data = parser->data;
    if (!reserve((void **)&data->materials, &parser->material_capacity,
                 data->material_count + 1, sizeof(*data->materials))) {
        return NULL;
    }

    at_model_material_data *material = &data->materials[data->material_count++];
    memset(material, 0, sizeof(*material));
    copy_name(material->name, sizeof(material->name), name, length);
    material->material = at_material_make();
    return material;
}

/* Reads materials from MTL text. Lines that can't be read are skipped */
/* with a warning; false only if out of memory.                       */
static bool parse_mtl(obj_parser *parser, const char *text, size_t size, const char *source,
                      const char *folder) {
    const char *saved_source = parser->source;
    uint32_t    saved_line   = parser->line;
    parser->source           = source;
    parser->line             = 0;

    const char             *cursor   = text;
    const char             *text_end = text + size;
    at_model_material_data *current  = NULL;
    bool                    ok       = true;
    text_line               line;

    while (next_line(&cursor, text_end, &line)) {
        parser->line++;

        const char *word;
        size_t      length;
        if (!next_word(&line, &word, &length)) {
            continue;
        }

        if (word_is(word, length, "newmtl")) {
            const char *name;
            size_t      name_length;
            rest_of_line(&line, &name, &name_length);
            current = add_material(parser, name, name_length);
            if (!current) {
                ok = false;
                break;
            }
            continue;
        }
        if (!current) {
            continue; /* Nothing to apply it to */
        }

        at_material *material = &current->material;
        float        rgb[3];
        float        value;

        if (word_is(word, length, "Kd")) {
            if (parse_rgb(&line, rgb)) {
                material->base_color.r = linear_to_srgb(rgb[0]);
                material->base_color.g = linear_to_srgb(rgb[1]);
                material->base_color.b = linear_to_srgb(rgb[2]);
            } else {
                parse_error(parser, "bad Kd color, skipped");
            }
        } else if (word_is(word, length, "Ks")) {
            if (parse_rgb(&line, rgb)) {
                material->specular = at_maxf((rgb[0] + rgb[1] + rgb[2]) / 3.0f, 0.0f);
            } else {
                parse_error(parser, "bad Ks color, skipped");
            }
        } else if (word_is(word, length, "Ke")) {
            if (parse_rgb(&line, rgb)) {
                /* Brighter than 1: the color, with the rest as its strength */
                float strength = at_maxf(at_maxf(rgb[0], rgb[1]), rgb[2]);
                strength       = at_maxf(strength, 1.0f);
                material->emissive          = at_color_rgb(linear_to_srgb(rgb[0] / strength),
                                                           linear_to_srgb(rgb[1] / strength),
                                                           linear_to_srgb(rgb[2] / strength));
                material->emissive_strength = strength;
            } else {
                parse_error(parser, "bad Ke color, skipped");
            }
        } else if (word_is(word, length, "Ns")) {
            if (parse_floats(&line, &value, 1) == 1) {
                material->shininess = at_clampf(value, 1.0f, 2048.0f);
            } else {
                parse_error(parser, "bad Ns value, skipped");
            }
        } else if (word_is(word, length, "d") || word_is(word, length, "Tr")) {
            if (parse_floats(&line, &value, 1) == 1) {
                float alpha = word_is(word, length, "Tr") ? 1.0f - value : value;
                material->base_color.a = at_clampf(alpha, 0.0f, 1.0f);
                material->transparent  = material->base_color.a < 0.999f;
            } else {
                parse_error(parser, "bad %.*s value, skipped", (int)length, word);
            }
        } else if (word_is(word, length, "map_Kd")) {
            parse_texture_path(parser, &line, folder, current->base_texture_path);
        } else if (word_is(word, length, "map_Bump") || word_is(word, length, "map_bump") ||
                   word_is(word, length, "bump") || word_is(word, length, "norm")) {
            parse_texture_path(parser, &line, folder, current->normal_texture_path);
        }
        /* Everything else (Ka, Ni, illum, map_Ks...) is not used */
    }

    parser->source = saved_source;
    parser->line   = saved_line;
    return ok;
}

/* Loads an MTL file named by mtllib, relative to the OBJ's folder */
static bool load_mtl(obj_parser *parser, const char *name, size_t length) {
    char path[AT_MODEL_PATH_MAX];
    if (!join_path(path, sizeof(path), parser->folder, name, length)) {
        parse_error(parser, "mtllib path is too long, skipped");
        return true;
    }

    size_t size = 0;
    char  *text = (char *)at_file_read_all(path, &size);
    if (!text) {
        fprintf(stderr, "[archtype] OBJ %s: could not read the materials (%s); "
                        "using default materials\n", parser->source, path);
        return true;
    }

    char   folder[AT_MODEL_PATH_MAX];
    size_t folder_size = folder_length(path);
    memcpy(folder, path, folder_size);
    folder[folder_size] = '\0';

    bool ok = parse_mtl(parser, text, size, path, folder);
    free(text);
    return ok;
}

/* ------------------------------------------------------------------------ */
/* Parts and vertices                                                        */
/* ------------------------------------------------------------------------ */

static uint32_t hash_corner(obj_corner corner) {
    uint32_t hash = 2166136261u;
    hash = (hash ^ (uint32_t)corner.position) * 16777619u;
    hash = (hash ^ (uint32_t)corner.uv) * 16777619u;
    hash = (hash ^ (uint32_t)corner.normal) * 16777619u;
    return hash ^ (hash >> 16);
}

static bool same_corner(obj_corner a, obj_corner b) {
    return a.position == b.position && a.uv == b.uv && a.normal == b.normal;
}

static void free_part(obj_part *part) {
    free(part->vertices);
    free(part->keys);
    free(part->indices);
    free(part->table);
}

/* Rebuilds the corner table with room for more vertices */
static bool grow_table(obj_part *part) {
    uint32_t  capacity = part->table_capacity ? part->table_capacity * 2 : 64;
    uint32_t *table    = (uint32_t *)malloc((size_t)capacity * sizeof(*table));
    if (!table) {
        return false;
    }
    memset(table, 0xFF, (size_t)capacity * sizeof(*table)); /* UINT32_MAX: empty */

    for (uint32_t i = 0; i < part->vertex_count; ++i) {
        uint32_t slot = hash_corner(part->keys[i]) & (capacity - 1);
        while (table[slot] != UINT32_MAX) {
            slot = (slot + 1) & (capacity - 1);
        }
        table[slot] = i;
    }

    free(part->table);
    part->table          = table;
    part->table_capacity = capacity;
    return true;
}

/* The part the next face goes to */
static obj_part *current_part(obj_parser *parser) {
    if (parser->part_count > 0 && !parser->new_part) {
        return &parser->parts[parser->part_count - 1];
    }
    parser->new_part = false;

    /* The last part has no faces yet: reuse it */
    obj_part *part = NULL;
    if (parser->part_count > 0 && parser->parts[parser->part_count - 1].index_count == 0) {
        part = &parser->parts[parser->part_count - 1];
    } else {
        if (!reserve((void **)&parser->parts, &parser->part_capacity, parser->part_count + 1,
                     sizeof(*parser->parts))) {
            return NULL;
        }
        part = &parser->parts[parser->part_count++];
        memset(part, 0, sizeof(*part));
    }

    memcpy(part->name, parser->name, sizeof(part->name));
    memcpy(part->material, parser->material, sizeof(part->material));
    return part;
}

/* The vertex of a corner, added the first time it is seen. */
/* UINT32_MAX if out of memory.                              */
static uint32_t corner_vertex(obj_parser *parser, obj_part *part, obj_corner corner) {
    if ((part->vertex_count + 1) * 2 > part->table_capacity && !grow_table(part)) {
        return UINT32_MAX;
    }

    uint32_t slot = hash_corner(corner) & (part->table_capacity - 1);
    while (part->table[slot] != UINT32_MAX) {
        uint32_t vertex = part->table[slot];
        if (same_corner(part->keys[vertex], corner)) {
            return vertex;
        }
        slot = (slot + 1) & (part->table_capacity - 1);
    }

    if (!reserve((void **)&part->vertices, &part->vertex_capacity, part->vertex_count + 1,
                 sizeof(*part->vertices))) {
        return UINT32_MAX;
    }
    if (!reserve((void **)&part->keys, &part->key_capacity, part->vertex_count + 1,
                 sizeof(*part->keys))) {
        return UINT32_MAX;
    }

    uint32_t     index  = part->vertex_count++;
    at_vertex3d *vertex = &part->vertices[index];
    memset(vertex, 0, sizeof(*vertex));
    part->keys[index] = corner;
    part->table[slot] = index;

    memcpy(vertex->position, &parser->positions[(size_t)corner.position * 3], 3 * sizeof(float));
    if (corner.uv != OBJ_MISSING) {
        vertex->uv[0] = parser->uvs[(size_t)corner.uv * 2];
        vertex->uv[1] = 1.0f - parser->uvs[(size_t)corner.uv * 2 + 1]; /* OBJ: (0,0) at bottom */
    }
    if (corner.normal != OBJ_MISSING) {
        memcpy(vertex->normal, &parser->normals[(size_t)corner.normal * 3], 3 * sizeof(float));
    } else {
        part->missing_normals = true;
    }
    return index;
}

/* ------------------------------------------------------------------------ */
/* OBJ lines                                                                 */
/* ------------------------------------------------------------------------ */

/* An index from the file (from 1, or negative from the end) to one from */
/* 0, checked against how many were defined so far                       */
static bool resolve_index(const obj_parser *parser, long index, uint32_t count,
                          const char *what, int32_t *out) {
    int64_t resolved = index > 0 ? (int64_t)index - 1 : (int64_t)count + index;
    if (index == 0 || resolved < 0 || resolved >= (int64_t)count) {
        parse_error(parser, "%s index %ld is out of range (%u defined so far)", what, index,
                    (unsigned)count);
        return false;
    }
    *out = (int32_t)resolved;
    return true;
}

/* "p", "p/t", "p//n" or "p/t/n" */
static bool parse_corner(const obj_parser *parser, const char *word, size_t length,
                         obj_corner *corner) {
    const char *c   = word;
    const char *end = word + length;

    corner->uv     = OBJ_MISSING;
    corner->normal = OBJ_MISSING;

    /* Read the numbers first; check their range after */
    long position = 0;
    long uv       = 0; /* 0: not given */
    long normal   = 0;

    bool ok = parse_index(&c, end, &position);
    if (ok && c < end && *c == '/') {
        c++;
        if (c < end && *c != '/') {
            ok = parse_index(&c, end, &uv) && uv != 0;
        }
        if (ok && c < end && *c == '/') {
            c++;
            ok = parse_index(&c, end, &normal) && normal != 0;
        }
    }
    if (!ok || c != end) {
        parse_error(parser, "bad face corner \"%.*s\"", (int)length, word);
        return false;
    }

    return resolve_index(parser, position, parser->position_count, "position",
                         &corner->position)
        && (uv == 0 || resolve_index(parser, uv, parser->uv_count, "texture coordinate",
                                     &corner->uv))
        && (normal == 0 || resolve_index(parser, normal, parser->normal_count, "normal",
                                         &corner->normal));
}

static bool parse_face(obj_parser *parser, text_line *line) {
    uint32_t    count = 0;
    const char *word;
    size_t      length;

    while (next_word(line, &word, &length)) {
        if (!reserve((void **)&parser->face, &parser->face_capacity, count + 1,
                     sizeof(*parser->face))) {
            parse_error(parser, "out of memory");
            return false;
        }
        if (!parse_corner(parser, word, length, &parser->face[count])) {
            return false;
        }
        count++;
    }

    if (count < 3) {
        parse_error(parser, "face with %u corners skipped (at least 3 are needed)",
                    (unsigned)count);
        return true;
    }

    obj_part *part = current_part(parser);
    if (!part) {
        parse_error(parser, "out of memory");
        return false;
    }

    /* A fan from the first corner: (0, 1, 2), (0, 2, 3)... */
    uint32_t first = corner_vertex(parser, part, parser->face[0]);
    uint32_t prev  = corner_vertex(parser, part, parser->face[1]);
    for (uint32_t i = 2; i < count; ++i) {
        uint32_t next = corner_vertex(parser, part, parser->face[i]);
        if (first == UINT32_MAX || prev == UINT32_MAX || next == UINT32_MAX ||
            !reserve((void **)&part->indices, &part->index_capacity, part->index_count + 3,
                     sizeof(*part->indices))) {
            parse_error(parser, "out of memory");
            return false;
        }
        part->indices[part->index_count++] = first;
        part->indices[part->index_count++] = prev;
        part->indices[part->index_count++] = next;
        prev = next;
    }
    return true;
}

/* v, vt and vn: appends count floats, of which at least min_count must */
/* be in the line (the rest are 0)                                      */
static bool parse_attribute(obj_parser *parser, text_line *line, float **items,
                            uint32_t *item_count, uint32_t *capacity, int count,
                            int min_count, const char *what) {
    float values[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    int   read      = parse_floats(line, values, count);

    /* Extra values (vertex colors, a w) are allowed and ignored */
    if (read < min_count) {
        parse_error(parser, "bad %s line", what);
        return false;
    }

    uint32_t floats = (*item_count + 1) * (uint32_t)count;
    if (*item_count >= 0x7FFFFFFFu / 4 ||
        !reserve((void **)items, capacity, floats, sizeof(float))) {
        parse_error(parser, "out of memory");
        return false;
    }
    memcpy(&(*items)[(size_t)*item_count * (size_t)count], values, (size_t)count * sizeof(float));
    (*item_count)++;
    return true;
}

static bool parse_obj_text(obj_parser *parser, const char *text, size_t size) {
    const char *cursor   = text;
    const char *text_end = text + size;
    text_line   line;

    while (next_line(&cursor, text_end, &line)) {
        parser->line++;

        const char *word;
        size_t      length;
        if (!next_word(&line, &word, &length)) {
            continue;
        }

        bool ok = true;
        if (word_is(word, length, "v")) {
            ok = parse_attribute(parser, &line, &parser->positions, &parser->position_count,
                                 &parser->position_capacity, 3, 3, "v");
        } else if (word_is(word, length, "vt")) {
            ok = parse_attribute(parser, &line, &parser->uvs, &parser->uv_count,
                                 &parser->uv_capacity, 2, 1, "vt");
        } else if (word_is(word, length, "vn")) {
            ok = parse_attribute(parser, &line, &parser->normals, &parser->normal_count,
                                 &parser->normal_capacity, 3, 3, "vn");
        } else if (word_is(word, length, "f")) {
            ok = parse_face(parser, &line);
        } else if (word_is(word, length, "o") || word_is(word, length, "g")) {
            const char *name;
            size_t      name_length;
            rest_of_line(&line, &name, &name_length);
            copy_name(parser->name, sizeof(parser->name), name, name_length);
            parser->new_part = true;
        } else if (word_is(word, length, "usemtl")) {
            const char *name;
            size_t      name_length;
            rest_of_line(&line, &name, &name_length);
            copy_name(parser->material, sizeof(parser->material), name, name_length);
            parser->new_part = true;
        } else if (word_is(word, length, "mtllib")) {
            const char *name;
            size_t      name_length;
            rest_of_line(&line, &name, &name_length);
            if (parser->folder && name_length > 0) {
                ok = load_mtl(parser, name, name_length);
            }
        }
        /* Everything else (s, l, p, cstype...) is not used */

        if (!ok) {
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* Result                                                                    */
/* ------------------------------------------------------------------------ */

static bool find_material(obj_parser *parser, const char *name, uint32_t *index) {
    at_model_data *data = parser->data;
    for (uint32_t i = 0; i < data->material_count; ++i) {
        if (strcmp(data->materials[i].name, name) == 0) {
            *index = i;
            return true;
        }
    }
    return false;
}

/* Turns the parts into the result; false if out of memory or no faces */
static bool finish(obj_parser *parser) {
    at_model_data *data = parser->data;

    uint32_t face_parts = 0;
    for (uint32_t i = 0; i < parser->part_count; ++i) {
        face_parts += parser->parts[i].index_count > 0 ? 1u : 0u;
    }
    if (face_parts == 0) {
        fprintf(stderr, "[archtype] OBJ %s: the file has no faces\n", parser->source);
        return false;
    }

    data->parts = (at_model_part_data *)calloc(face_parts, sizeof(*data->parts));
    if (!data->parts) {
        fprintf(stderr, "[archtype] OBJ %s: out of memory\n", parser->source);
        return false;
    }

    for (uint32_t i = 0; i < parser->part_count; ++i) {
        obj_part *part = &parser->parts[i];
        if (part->index_count == 0) {
            continue;
        }

        uint32_t material = 0;
        if (part->material[0] == '\0' || !find_material(parser, part->material, &material)) {
            if (part->material[0] != '\0') {
                fprintf(stderr, "[archtype] OBJ %s: material \"%s\" not found, using the "
                                "default\n", parser->source, part->material);
            }
            if (!find_material(parser, "default", &material)) {
                if (!add_material(parser, "default", strlen("default"))) {
                    fprintf(stderr, "[archtype] OBJ %s: out of memory\n", parser->source);
                    return false;
                }
                material = data->material_count - 1;
            }
        }

        at_model_part_data *out = &data->parts[data->part_count++];
        memcpy(out->name, part->name, sizeof(out->name));
        out->material           = material;
        out->mesh.vertices      = part->vertices;
        out->mesh.vertex_count  = part->vertex_count;
        out->mesh.indices       = part->indices;
        out->mesh.index_count   = part->index_count;
        part->vertices          = NULL; /* Owned by the result now */
        part->indices           = NULL;

        if (part->missing_normals) {
            at_mesh_data_compute_normals(&out->mesh);
        }
        at_mesh_data_compute_tangents(&out->mesh);
    }
    return true;
}

static void parser_free(obj_parser *parser) {
    for (uint32_t i = 0; i < parser->part_count; ++i) {
        free_part(&parser->parts[i]);
    }
    free(parser->parts);
    free(parser->positions);
    free(parser->uvs);
    free(parser->normals);
    free(parser->face);
}

static void parser_init(obj_parser *parser, at_model_data *data, const char *source,
                        const char *folder) {
    memset(parser, 0, sizeof(*parser));
    memset(data, 0, sizeof(*data));
    parser->data   = data;
    parser->source = source;
    parser->folder = folder;
}

/* ------------------------------------------------------------------------ */
/* Public functions                                                          */
/* ------------------------------------------------------------------------ */

bool at_model_data_parse_obj(at_model_data *data, const char *obj, size_t obj_size,
                             const char *mtl, size_t mtl_size) {
    if (!data) {
        return false;
    }

    obj_parser parser;
    parser_init(&parser, data, "<memory>", NULL);
    if (!obj) {
        fprintf(stderr, "[archtype] at_model_data_parse_obj needs OBJ text\n");
        return false;
    }

    bool ok = (!mtl || parse_mtl(&parser, mtl, mtl_size, "<memory mtl>", NULL))
           && parse_obj_text(&parser, obj, obj_size)
           && finish(&parser);

    parser_free(&parser);
    if (!ok) {
        at_model_data_free(data);
    }
    return ok;
}

bool at_model_data_load_obj(at_model_data *data, const char *path) {
    if (!data) {
        return false;
    }
    memset(data, 0, sizeof(*data));
    if (!path) {
        return false;
    }

    char   folder[AT_MODEL_PATH_MAX];
    size_t folder_size = folder_length(path);
    if (folder_size >= sizeof(folder)) {
        fprintf(stderr, "[archtype] Model path is too long: %s\n", path);
        return false;
    }
    memcpy(folder, path, folder_size);
    folder[folder_size] = '\0';

    size_t size = 0;
    char  *text = (char *)at_file_read_all(path, &size);
    if (!text) {
        return false;
    }

    obj_parser parser;
    parser_init(&parser, data, path, folder);

    bool ok = parse_obj_text(&parser, text, size) && finish(&parser);

    parser_free(&parser);
    free(text);
    if (!ok) {
        at_model_data_free(data);
    }
    return ok;
}

void at_model_data_free(at_model_data *data) {
    if (!data) {
        return;
    }
    for (uint32_t i = 0; i < data->part_count; ++i) {
        at_mesh_data_free(&data->parts[i].mesh);
    }
    free(data->parts);
    free(data->materials);
    memset(data, 0, sizeof(*data));
}