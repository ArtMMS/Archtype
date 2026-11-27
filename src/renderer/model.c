#include "archtype/renderer/model.h"
#include "archtype/renderer/texture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct at_model {
    at_mesh    **meshes;          /* One per part                         */
    uint32_t    *part_materials;  /* Material index of each part          */
    uint32_t     part_count;

    at_material *materials;
    uint32_t     material_count;

    at_texture **textures;        /* Loaded by the model, destroyed with it */
    uint32_t     texture_count;

    uint32_t     triangle_count;
    at_vec3      bounds_min;
    at_vec3      bounds_max;
};

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

static bool has_extension(const char *path, const char *extension) {
    size_t path_length      = strlen(path);
    size_t extension_length = strlen(extension);
    if (path_length < extension_length) {
        return false;
    }

    const char *end = path + path_length - extension_length;
    for (size_t i = 0; i < extension_length; ++i) {
        char c = end[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if (c != extension[i]) {
            return false;
        }
    }
    return true;
}

/* Loads a texture once: materials naming the same file share it.   */
/* paths[i] is the file of textures[i]. NULL if it failed to load. */
static at_texture *load_texture(at_renderer *renderer, at_model *model, const char **paths,
                                const char *path) {
    for (uint32_t i = 0; i < model->texture_count; ++i) {
        if (strcmp(paths[i], path) == 0) {
            return model->textures[i];
        }
    }

    at_texture_options options;
    memset(&options, 0, sizeof(options));
    options.filter      = AT_TEXTURE_FILTER_LINEAR;
    options.wrap        = AT_TEXTURE_WRAP_REPEAT;
    options.color_space = AT_TEXTURE_COLOR_SPACE_SRGB;
    options.mipmaps     = true;

    at_texture *texture = at_texture_load(renderer, path, &options);
    if (!texture) {
        fprintf(stderr, "[archtype] Model texture left out: %s\n", path);
    }

    /* Remembered even when it failed, so it is only tried once */
    paths[model->texture_count]           = path;
    model->textures[model->texture_count] = texture;
    model->texture_count++;
    return texture;
}

static void grow_bounds(at_model *model, const at_mesh_data *mesh, bool *first) {
    for (uint32_t i = 0; i < mesh->vertex_count; ++i) {
        const float *p = mesh->vertices[i].position;
        if (*first) {
            model->bounds_min = at_vec3_make(p[0], p[1], p[2]);
            model->bounds_max = model->bounds_min;
            *first            = false;
            continue;
        }
        model->bounds_min.x = at_minf(model->bounds_min.x, p[0]);
        model->bounds_min.y = at_minf(model->bounds_min.y, p[1]);
        model->bounds_min.z = at_minf(model->bounds_min.z, p[2]);
        model->bounds_max.x = at_maxf(model->bounds_max.x, p[0]);
        model->bounds_max.y = at_maxf(model->bounds_max.y, p[1]);
        model->bounds_max.z = at_maxf(model->bounds_max.z, p[2]);
    }
}

/* ------------------------------------------------------------------------ */
/* Creation and destruction                                                  */
/* ------------------------------------------------------------------------ */

at_model *at_model_create(at_renderer *renderer, const at_model_data *data) {
    if (!renderer || !data || data->part_count == 0 || data->material_count == 0) {
        fprintf(stderr, "[archtype] at_model_create needs a renderer and model data "
                        "with parts and materials\n");
        return NULL;
    }

    at_model *model = (at_model *)calloc(1, sizeof(*model));
    if (!model) {
        fprintf(stderr, "[archtype] Failed to allocate a model\n");
        return NULL;
    }

    /* At most one texture per material slot (base and normal) */
    uint32_t     max_textures = data->material_count * 2;
    const char **paths        = (const char **)calloc(max_textures, sizeof(*paths));

    model->meshes         = (at_mesh **)calloc(data->part_count, sizeof(*model->meshes));
    model->part_materials = (uint32_t *)calloc(data->part_count,
                                               sizeof(*model->part_materials));
    model->materials      = (at_material *)calloc(data->material_count,
                                                  sizeof(*model->materials));
    model->textures       = (at_texture **)calloc(max_textures, sizeof(*model->textures));

    if (!paths || !model->meshes || !model->part_materials || !model->materials ||
        !model->textures) {
        fprintf(stderr, "[archtype] Failed to allocate a model\n");
        free((void *)paths);
        at_model_destroy(renderer, model);
        return NULL;
    }

    /* Materials, with their textures. The normal texture is only read for */
    /* now: the 3D renderer will use it with normal maps.                  */
    model->material_count = data->material_count;
    for (uint32_t i = 0; i < data->material_count; ++i) {
        const at_model_material_data *source = &data->materials[i];
        model->materials[i]                  = source->material;
        if (source->base_texture_path[0] != '\0') {
            model->materials[i].base_texture = load_texture(renderer, model, paths,
                                                            source->base_texture_path);
        }
    }
    free((void *)paths); /* They pointed into data */

    /* Meshes */
    bool first = true;
    for (uint32_t i = 0; i < data->part_count; ++i) {
        const at_model_part_data *part = &data->parts[i];

        model->meshes[i] = at_mesh_create(renderer, &part->mesh);
        if (!model->meshes[i]) {
            at_model_destroy(renderer, model);
            return NULL;
        }
        model->part_count++;
        model->part_materials[i] = part->material < data->material_count ? part->material : 0;
        model->triangle_count   += part->mesh.index_count / 3;
        grow_bounds(model, &part->mesh, &first);
    }

    return model;
}

at_model *at_model_load(at_renderer *renderer, const char *path) {
    if (!renderer || !path) {
        return NULL;
    }
    if (!has_extension(path, ".obj")) {
        fprintf(stderr, "[archtype] Unknown model format (only .obj for now): %s\n", path);
        return NULL;
    }

    at_model_data data;
    if (!at_model_data_load_obj(&data, path)) {
        return NULL;
    }

    at_model *model = at_model_create(renderer, &data);
    at_model_data_free(&data);

    if (model) {
        fprintf(stderr, "[archtype] Model loaded: %s (%u parts, %u materials, %u triangles)\n",
                path, (unsigned)model->part_count, (unsigned)model->material_count,
                (unsigned)model->triangle_count);
    }
    return model;
}

void at_model_destroy(at_renderer *renderer, at_model *model) {
    if (!model) {
        return;
    }

    if (renderer) {
        for (uint32_t i = 0; i < model->part_count; ++i) {
            at_mesh_destroy(renderer, model->meshes[i]);
        }
        for (uint32_t i = 0; i < model->texture_count; ++i) {
            at_texture_destroy(renderer, model->textures[i]);
        }
    }

    free(model->meshes);
    free(model->part_materials);
    free(model->materials);
    free(model->textures);
    free(model);
}

/* ------------------------------------------------------------------------ */
/* Drawing and queries                                                       */
/* ------------------------------------------------------------------------ */

void at_model_draw(at_renderer3d *renderer3d, const at_model *model, at_mat4 transform) {
    if (!renderer3d || !model) {
        return;
    }
    for (uint32_t i = 0; i < model->part_count; ++i) {
        at_renderer3d_draw_mesh_ex(renderer3d, model->meshes[i], transform,
                                   &model->materials[model->part_materials[i]]);
    }
}

uint32_t at_model_get_part_count(const at_model *model) {
    return model ? model->part_count : 0;
}

uint32_t at_model_get_material_count(const at_model *model) {
    return model ? model->material_count : 0;
}

uint32_t at_model_get_triangle_count(const at_model *model) {
    return model ? model->triangle_count : 0;
}

at_material *at_model_get_material(at_model *model, uint32_t index) {
    if (!model || index >= model->material_count) {
        return NULL;
    }
    return &model->materials[index];
}

void at_model_get_bounds(const at_model *model, at_vec3 *min, at_vec3 *max) {
    at_vec3 zero = at_vec3_zero();
    if (min) {
        *min = model ? model->bounds_min : zero;
    }
    if (max) {
        *max = model ? model->bounds_max : zero;
    }
}