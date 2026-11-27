#include "archtype/renderer/mesh_utils.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Small vector helpers on float[3]                                         */
/* ------------------------------------------------------------------------ */

static void sub3(float out[3], const float a[3], const float b[3]) {
    out[0] = a[0] - b[0];
    out[1] = a[1] - b[1];
    out[2] = a[2] - b[2];
}

static void cross3(float out[3], const float a[3], const float b[3]) {
    float x = a[1] * b[2] - a[2] * b[1];
    float y = a[2] * b[0] - a[0] * b[2];
    float z = a[0] * b[1] - a[1] * b[0];
    out[0]  = x;
    out[1]  = y;
    out[2]  = z;
}

static float dot3(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void add3(float out[3], const float a[3]) {
    out[0] += a[0];
    out[1] += a[1];
    out[2] += a[2];
}

/* Normalizes v; returns false (v unchanged) if it has no length */
static bool normalize3(float v[3]) {
    float length_sq = dot3(v, v);
    if (!(length_sq > 1e-20f)) {
        return false;
    }
    float inverse = 1.0f / sqrtf(length_sq);
    v[0] *= inverse;
    v[1] *= inverse;
    v[2] *= inverse;
    return true;
}

/* Any unit direction perpendicular to the unit vector n */
static void any_perpendicular(float out[3], const float n[3]) {
    /* Cross with the axis least aligned with n */
    const float x_axis[3] = { 1.0f, 0.0f, 0.0f };
    const float y_axis[3] = { 0.0f, 1.0f, 0.0f };
    cross3(out, fabsf(n[0]) < 0.9f ? x_axis : y_axis, n);
    if (!normalize3(out)) {
        out[0] = 1.0f;
        out[1] = 0.0f;
        out[2] = 0.0f;
    }
}

static bool valid_triangle(const at_mesh_data *data, uint32_t first) {
    return data->indices[first] < data->vertex_count
        && data->indices[first + 1] < data->vertex_count
        && data->indices[first + 2] < data->vertex_count;
}

/* ------------------------------------------------------------------------ */
/* Normals                                                                   */
/* ------------------------------------------------------------------------ */

/* Positions are compared on a fine grid (a millionth of the mesh's size), */
/* so vertices that should be at the same place but differ by rounding     */
/* errors (like the poles of a sphere) still share their normal.           */
typedef struct grid_key {
    int64_t x, y, z;
} grid_key;

static grid_key make_key(const float p[3], double step) {
    grid_key key;
    key.x = (int64_t)floor((double)p[0] / step + 0.5);
    key.y = (int64_t)floor((double)p[1] / step + 0.5);
    key.z = (int64_t)floor((double)p[2] / step + 0.5);
    return key;
}

static uint32_t hash_key(grid_key key) {
    uint64_t hash = 1469598103934665603ull;
    hash = (hash ^ (uint64_t)key.x) * 1099511628211ull;
    hash = (hash ^ (uint64_t)key.y) * 1099511628211ull;
    hash = (hash ^ (uint64_t)key.z) * 1099511628211ull;
    return (uint32_t)(hash ^ (hash >> 32));
}

static bool same_key(grid_key a, grid_key b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

/* For each vertex, the first vertex at the same position (its "group"). */
/* Returns NULL if out of memory.                                        */
static uint32_t *group_by_position(const at_mesh_data *data) {
    uint32_t  count  = data->vertex_count;
    uint32_t *groups = (uint32_t *)malloc((size_t)count * sizeof(*groups));
    grid_key *keys   = (grid_key *)malloc((size_t)count * sizeof(*keys));

    /* Open addressing table, at most half full */
    uint32_t capacity = 16;
    while (capacity < count * 2u && capacity < 0x80000000u) {
        capacity *= 2;
    }
    uint32_t *table = (uint32_t *)malloc((size_t)capacity * sizeof(*table));

    if (!groups || !keys || !table) {
        free(groups);
        free(keys);
        free(table);
        return NULL;
    }
    memset(table, 0xFF, (size_t)capacity * sizeof(*table)); /* UINT32_MAX: empty */

    /* Grid step from the size of the mesh */
    double size = 0.0;
    for (uint32_t i = 0; i < count; ++i) {
        for (int k = 0; k < 3; ++k) {
            double value = fabs((double)data->vertices[i].position[k]);
            size         = value > size ? value : size;
        }
    }
    double step = size > 0.0 ? size * 1e-6 : 1.0;

    for (uint32_t i = 0; i < count; ++i) {
        keys[i]       = make_key(data->vertices[i].position, step);
        uint32_t slot = hash_key(keys[i]) & (capacity - 1);

        for (;;) {
            uint32_t other = table[slot];
            if (other == UINT32_MAX) {
                table[slot] = i;
                groups[i]   = i;
                break;
            }
            if (same_key(keys[other], keys[i])) {
                groups[i] = other;
                break;
            }
            slot = (slot + 1) & (capacity - 1);
        }
    }

    free(keys);
    free(table);
    return groups;
}

void at_mesh_data_compute_normals(at_mesh_data *data) {
    if (!data || !data->vertices || data->vertex_count == 0) {
        return;
    }

    uint32_t *groups = group_by_position(data);
    float    *sums   = (float *)calloc((size_t)data->vertex_count * 3, sizeof(*sums));
    if (!groups || !sums) {
        fprintf(stderr, "[archtype] Out of memory computing mesh normals\n");
        free(groups);
        free(sums);
        return;
    }

    /* The cross product of two edges points out of the front face and is */
    /* as long as twice the triangle's area: summing it weights by area.  */
    for (uint32_t i = 0; data->indices && i + 2 < data->index_count; i += 3) {
        if (!valid_triangle(data, i)) {
            continue;
        }
        const float *p0 = data->vertices[data->indices[i]].position;
        const float *p1 = data->vertices[data->indices[i + 1]].position;
        const float *p2 = data->vertices[data->indices[i + 2]].position;

        float e1[3], e2[3], face[3];
        sub3(e1, p1, p0);
        sub3(e2, p2, p0);
        cross3(face, e1, e2);

        for (int corner = 0; corner < 3; ++corner) {
            uint32_t group = groups[data->indices[i + (uint32_t)corner]];
            add3(&sums[(size_t)group * 3], face);
        }
    }

    for (uint32_t i = 0; i < data->vertex_count; ++i) {
        float *normal = data->vertices[i].normal;
        memcpy(normal, &sums[(size_t)groups[i] * 3], 3 * sizeof(float));
        if (!normalize3(normal)) {
            normal[0] = 0.0f;
            normal[1] = 1.0f;
            normal[2] = 0.0f;
        }
    }

    free(groups);
    free(sums);
}

/* ------------------------------------------------------------------------ */
/* Tangents                                                                  */
/* ------------------------------------------------------------------------ */

void at_mesh_data_compute_tangents(at_mesh_data *data) {
    if (!data || !data->vertices || data->vertex_count == 0) {
        return;
    }

    /* Per vertex: how the position changes along +u and along +v */
    float *along_u = (float *)calloc((size_t)data->vertex_count * 3, sizeof(*along_u));
    float *along_v = (float *)calloc((size_t)data->vertex_count * 3, sizeof(*along_v));
    if (!along_u || !along_v) {
        fprintf(stderr, "[archtype] Out of memory computing mesh tangents\n");
        free(along_u);
        free(along_v);
        return;
    }

    for (uint32_t i = 0; data->indices && i + 2 < data->index_count; i += 3) {
        if (!valid_triangle(data, i)) {
            continue;
        }
        const at_vertex3d *v0 = &data->vertices[data->indices[i]];
        const at_vertex3d *v1 = &data->vertices[data->indices[i + 1]];
        const at_vertex3d *v2 = &data->vertices[data->indices[i + 2]];

        float e1[3], e2[3];
        sub3(e1, v1->position, v0->position);
        sub3(e2, v2->position, v0->position);

        float du1 = v1->uv[0] - v0->uv[0];
        float dv1 = v1->uv[1] - v0->uv[1];
        float du2 = v2->uv[0] - v0->uv[0];
        float dv2 = v2->uv[1] - v0->uv[1];

        /* Solve e1 = du1 * U + dv1 * V and e2 = du2 * U + dv2 * V */
        float det = du1 * dv2 - du2 * dv1;
        if (fabsf(det) < 1e-12f) {
            continue; /* The UVs don't span the triangle */
        }
        float r = 1.0f / det;

        float u_dir[3], v_dir[3];
        for (int k = 0; k < 3; ++k) {
            u_dir[k] = (e1[k] * dv2 - e2[k] * dv1) * r;
            v_dir[k] = (e2[k] * du1 - e1[k] * du2) * r;
        }

        for (int corner = 0; corner < 3; ++corner) {
            size_t vertex = (size_t)data->indices[i + (uint32_t)corner] * 3;
            add3(&along_u[vertex], u_dir);
            add3(&along_v[vertex], v_dir);
        }
    }

    for (uint32_t i = 0; i < data->vertex_count; ++i) {
        at_vertex3d *vertex = &data->vertices[i];

        float normal[3] = { vertex->normal[0], vertex->normal[1], vertex->normal[2] };
        if (!normalize3(normal)) {
            normal[0] = 0.0f;
            normal[1] = 1.0f;
            normal[2] = 0.0f;
        }

        /* Remove the part along the normal (Gram-Schmidt) */
        float *u_sum   = &along_u[(size_t)i * 3];
        float  along_n = dot3(u_sum, normal);
        float  tangent[3];
        for (int k = 0; k < 3; ++k) {
            tangent[k] = u_sum[k] - normal[k] * along_n;
        }

        float w = 1.0f;
        if (normalize3(tangent)) {
            /* Up in the image is -v (v grows downward) */
            float bitangent[3];
            cross3(bitangent, normal, tangent);
            if (dot3(bitangent, &along_v[(size_t)i * 3]) > 0.0f) {
                w = -1.0f;
            }
        } else {
            any_perpendicular(tangent, normal);
        }

        vertex->tangent[0] = tangent[0];
        vertex->tangent[1] = tangent[1];
        vertex->tangent[2] = tangent[2];
        vertex->tangent[3] = w;
    }

    free(along_u);
    free(along_v);
}