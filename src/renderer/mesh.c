#include "archtype/renderer/mesh.h"
#include "archtype/renderer/buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct at_mesh {
    at_buffer *vertex_buffer;
    at_buffer *index_buffer; /* 32-bit indices */
    uint32_t   vertex_count;
    uint32_t   index_count;
};

at_mesh *at_mesh_create(at_renderer *renderer, const at_mesh_data *data) {
    if (!renderer || !data || !data->vertices || !data->indices ||
        data->vertex_count == 0 || data->index_count == 0) {
        fprintf(stderr, "[archtype] Invalid mesh data (vertices and indices are required)\n");
        return NULL;
    }

    /* A wrong index would make the GPU read outside the vertex buffer */
    for (uint32_t i = 0; i < data->index_count; ++i) {
        if (data->indices[i] >= data->vertex_count) {
            fprintf(stderr, "[archtype] Invalid mesh data: index %u is %u, but there are "
                            "only %u vertices\n", i, data->indices[i], data->vertex_count);
            return NULL;
        }
    }

    at_mesh *mesh = (at_mesh *)calloc(1, sizeof(*mesh));
    if (!mesh) {
        fprintf(stderr, "[archtype] Failed to allocate mesh\n");
        return NULL;
    }

    at_buffer_desc vertex_desc;
    memset(&vertex_desc, 0, sizeof(vertex_desc));
    vertex_desc.type = AT_BUFFER_VERTEX;
    vertex_desc.size = (size_t)data->vertex_count * sizeof(at_vertex3d);
    vertex_desc.data = data->vertices;

    at_buffer_desc index_desc;
    memset(&index_desc, 0, sizeof(index_desc));
    index_desc.type       = AT_BUFFER_INDEX;
    index_desc.index_type = AT_INDEX_UINT32;
    index_desc.size       = (size_t)data->index_count * sizeof(uint32_t);
    index_desc.data       = data->indices;

    mesh->vertex_buffer = at_buffer_create(renderer, &vertex_desc);
    mesh->index_buffer  = at_buffer_create(renderer, &index_desc);
    mesh->vertex_count  = data->vertex_count;
    mesh->index_count   = data->index_count;

    if (!mesh->vertex_buffer || !mesh->index_buffer) {
        at_mesh_destroy(renderer, mesh);
        return NULL;
    }

    return mesh;
}

at_mesh *at_mesh_create_cube(at_renderer *renderer, at_vec3 size) {
    at_mesh_data data;
    if (!at_mesh_data_cube(&data, size)) {
        return NULL;
    }

    at_mesh *mesh = at_mesh_create(renderer, &data);
    at_mesh_data_free(&data);
    return mesh;
}

at_mesh *at_mesh_create_plane(at_renderer *renderer, float width, float depth,
                              uint32_t subdivisions_x, uint32_t subdivisions_z) {
    at_mesh_data data;
    if (!at_mesh_data_plane(&data, width, depth, subdivisions_x, subdivisions_z)) {
        return NULL;
    }

    at_mesh *mesh = at_mesh_create(renderer, &data);
    at_mesh_data_free(&data);
    return mesh;
}

at_mesh *at_mesh_create_sphere(at_renderer *renderer, float radius, uint32_t segments,
                               uint32_t rings) {
    at_mesh_data data;
    if (!at_mesh_data_sphere(&data, radius, segments, rings)) {
        return NULL;
    }

    at_mesh *mesh = at_mesh_create(renderer, &data);
    at_mesh_data_free(&data);
    return mesh;
}

void at_mesh_destroy(at_renderer *renderer, at_mesh *mesh) {
    if (!renderer || !mesh) {
        return;
    }

    at_buffer_destroy(renderer, mesh->index_buffer);
    at_buffer_destroy(renderer, mesh->vertex_buffer);
    free(mesh);
}

uint32_t at_mesh_get_vertex_count(const at_mesh *mesh) {
    return mesh ? mesh->vertex_count : 0;
}

uint32_t at_mesh_get_index_count(const at_mesh *mesh) {
    return mesh ? mesh->index_count : 0;
}

void at_mesh_draw(at_renderer *renderer, const at_mesh *mesh) {
    if (!renderer || !mesh) {
        return;
    }

    at_renderer_bind_vertex_buffer(renderer, mesh->vertex_buffer);
    at_renderer_bind_index_buffer(renderer, mesh->index_buffer);
    at_renderer_draw_indexed(renderer, mesh->index_count);
}