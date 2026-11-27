#include "archtype/renderer/mesh.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Most subdivisions, segments or rings: keeps the counts far from overflowing */
#define AT_MESH_MAX_DIVISIONS 4096

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

static bool allocate(at_mesh_data *data, uint32_t vertex_count, uint32_t index_count) {
    data->vertices = (at_vertex3d *)malloc((size_t)vertex_count * sizeof(at_vertex3d));
    data->indices  = (uint32_t *)malloc((size_t)index_count * sizeof(uint32_t));
    if (!data->vertices || !data->indices) {
        fprintf(stderr, "[archtype] Out of memory creating mesh data (%u vertices)\n",
                vertex_count);
        at_mesh_data_free(data);
        return false;
    }

    data->vertex_count = vertex_count;
    data->index_count  = index_count;
    return true;
}

/* The tangent's w is always +1 for the generated meshes: their */
/* texture is never mirrored.                                   */
static void set_vertex(at_vertex3d *vertex, at_vec3 position, at_vec3 normal,
                       float u, float v, at_vec3 tangent) {
    vertex->position[0] = position.x;
    vertex->position[1] = position.y;
    vertex->position[2] = position.z;
    vertex->normal[0]   = normal.x;
    vertex->normal[1]   = normal.y;
    vertex->normal[2]   = normal.z;
    vertex->uv[0]       = u;
    vertex->uv[1]       = v;
    vertex->tangent[0]  = tangent.x;
    vertex->tangent[1]  = tangent.y;
    vertex->tangent[2]  = tangent.z;
    vertex->tangent[3]  = 1.0f;
}

/* ------------------------------------------------------------------------ */
/* Cube                                                                      */
/* ------------------------------------------------------------------------ */

bool at_mesh_data_cube(at_mesh_data *data, at_vec3 size) {
    memset(data, 0, sizeof(*data));

    if (!(size.x > 0.0f && size.y > 0.0f && size.z > 0.0f)) {
        fprintf(stderr, "[archtype] at_mesh_data_cube: the size must be positive\n");
        return false;
    }
    if (!allocate(data, 24, 36)) {
        return false;
    }

    /* Every face is built from its normal and two axes, u (right on the    */
    /* face) and v (up on the face), with u x v = normal: its corners go    */
    /* counter-clockwise seen from outside. u is also the tangent, and      */
    /* normal x u = v is the bitangent, so w = +1.                          */
    static const float faces[6][3][3] = {
        /*  normal               u                    v                */
        { {  1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f,  0.0f } }, /* +X */
        { { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f,  1.0f }, { 0.0f, 1.0f,  0.0f } }, /* -X */
        { {  0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f,  0.0f }, { 0.0f, 0.0f, -1.0f } }, /* +Y */
        { {  0.0f,-1.0f, 0.0f }, { 1.0f, 0.0f,  0.0f }, { 0.0f, 0.0f,  1.0f } }, /* -Y */
        { {  0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f,  0.0f }, { 0.0f, 1.0f,  0.0f } }, /* +Z */
        { {  0.0f, 0.0f,-1.0f }, {-1.0f, 0.0f,  0.0f }, { 0.0f, 1.0f,  0.0f } }, /* -Z */
    };

    /* Corners in face axes: bottom-left, bottom-right, top-right, top-left */
    static const float corners[4][2] = {
        { -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f },
    };

    at_vec3 half = at_vec3_scale(size, 0.5f);

    for (uint32_t f = 0; f < 6; ++f) {
        at_vec3 n = at_vec3_make(faces[f][0][0], faces[f][0][1], faces[f][0][2]);
        at_vec3 u = at_vec3_make(faces[f][1][0], faces[f][1][1], faces[f][1][2]);
        at_vec3 v = at_vec3_make(faces[f][2][0], faces[f][2][1], faces[f][2][2]);

        for (uint32_t c = 0; c < 4; ++c) {
            float   cu    = corners[c][0];
            float   cv    = corners[c][1];
            at_vec3 local = at_vec3_add(n, at_vec3_add(at_vec3_scale(u, cu),
                                                       at_vec3_scale(v, cv)));

            /* Up on the face is the top of the image (v = 0) */
            set_vertex(&data->vertices[f * 4 + c], at_vec3_mul(local, half), n,
                       (cu + 1.0f) * 0.5f, (1.0f - cv) * 0.5f, u);
        }

        uint32_t  base = f * 4;
        uint32_t *tri  = &data->indices[f * 6];
        tri[0] = base;
        tri[1] = base + 1;
        tri[2] = base + 2;
        tri[3] = base;
        tri[4] = base + 2;
        tri[5] = base + 3;
    }

    return true;
}

/* ------------------------------------------------------------------------ */
/* Plane                                                                     */
/* ------------------------------------------------------------------------ */

bool at_mesh_data_plane(at_mesh_data *data, float width, float depth,
                        uint32_t subdivisions_x, uint32_t subdivisions_z) {
    memset(data, 0, sizeof(*data));

    if (!(width > 0.0f && depth > 0.0f)) {
        fprintf(stderr, "[archtype] at_mesh_data_plane: the size must be positive\n");
        return false;
    }
    if (subdivisions_x == 0) {
        subdivisions_x = 1;
    }
    if (subdivisions_z == 0) {
        subdivisions_z = 1;
    }
    if (subdivisions_x > AT_MESH_MAX_DIVISIONS || subdivisions_z > AT_MESH_MAX_DIVISIONS) {
        fprintf(stderr, "[archtype] at_mesh_data_plane: at most %d subdivisions\n",
                AT_MESH_MAX_DIVISIONS);
        return false;
    }

    uint32_t columns = subdivisions_x + 1; /* Vertices per row */
    uint32_t rows    = subdivisions_z + 1;
    if (!allocate(data, columns * rows, subdivisions_x * subdivisions_z * 6)) {
        return false;
    }

    /* Seen from above with -Z at the top, the image is upright: u grows */
    /* with +X and v with +Z. The bitangent, up x right = -Z, points to  */
    /* the top of the image.                                             */
    const at_vec3 up    = at_vec3_up();
    const at_vec3 right = at_vec3_right();

    for (uint32_t z = 0; z < rows; ++z) {
        for (uint32_t x = 0; x < columns; ++x) {
            float u = (float)x / (float)subdivisions_x;
            float v = (float)z / (float)subdivisions_z;

            at_vec3 position = at_vec3_make(-0.5f * width + width * u, 0.0f,
                                            -0.5f * depth + depth * v);
            set_vertex(&data->vertices[z * columns + x], position, up, u, v, right);
        }
    }

    /* Each square: a (here), b (toward +Z), c (toward +X and +Z), d (toward */
    /* +X). a, b, c and a, c, d are counter-clockwise seen from above.       */
    uint32_t *index = data->indices;
    for (uint32_t z = 0; z < subdivisions_z; ++z) {
        for (uint32_t x = 0; x < subdivisions_x; ++x) {
            uint32_t a = z * columns + x;
            uint32_t b = a + columns;
            uint32_t c = b + 1;
            uint32_t d = a + 1;

            *index++ = a;
            *index++ = b;
            *index++ = c;
            *index++ = a;
            *index++ = c;
            *index++ = d;
        }
    }

    return true;
}

/* ------------------------------------------------------------------------ */
/* Sphere                                                                    */
/* ------------------------------------------------------------------------ */

bool at_mesh_data_sphere(at_mesh_data *data, float radius, uint32_t segments, uint32_t rings) {
    memset(data, 0, sizeof(*data));

    if (!(radius > 0.0f)) {
        fprintf(stderr, "[archtype] at_mesh_data_sphere: the radius must be positive\n");
        return false;
    }
    if (segments < 3) {
        segments = 3;
    }
    if (rings < 2) {
        rings = 2;
    }
    if (segments > AT_MESH_MAX_DIVISIONS || rings > AT_MESH_MAX_DIVISIONS) {
        fprintf(stderr, "[archtype] at_mesh_data_sphere: at most %d segments and rings\n",
                AT_MESH_MAX_DIVISIONS);
        return false;
    }

    /* One more column than segments: the seam has two vertices in the same */
    /* place, one with u = 0 and one with u = 1. The poles have one vertex   */
    /* per segment too, so each triangle there gets its own u.               */
    uint32_t columns = segments + 1;
    if (!allocate(data, columns * (rings + 1), segments * (rings - 1) * 6)) {
        return false;
    }

    for (uint32_t ring = 0; ring <= rings; ++ring) {
        /* phi: 0 at the north pole, pi at the south pole. The poles are */
        /* set exactly, so their vertices meet in one point.             */
        float phi     = AT_PI * (float)ring / (float)rings;
        float sin_phi = sinf(phi);
        float cos_phi = cosf(phi);
        if (ring == 0 || ring == rings) {
            sin_phi = 0.0f;
            cos_phi = ring == 0 ? 1.0f : -1.0f;
        }

        for (uint32_t segment = 0; segment <= segments; ++segment) {
            /* theta: pi at the seam (-Z), going around through +X, +Z (front, */
            /* u = 0.5) and -X. The seam column reuses the angle of column 0   */
            /* so both have exactly the same position.                         */
            float theta     = AT_PI + AT_TAU * (float)(segment % segments) / (float)segments;
            float sin_theta = sinf(theta);
            float cos_theta = cosf(theta);

            at_vec3 normal = at_vec3_make(sin_phi * sin_theta, cos_phi, sin_phi * cos_theta);

            /* Where u grows: the derivative of the position by theta */
            at_vec3 tangent = at_vec3_make(cos_theta, 0.0f, -sin_theta);

            set_vertex(&data->vertices[ring * columns + segment],
                       at_vec3_scale(normal, radius), normal,
                       (float)segment / (float)segments, (float)ring / (float)rings, tangent);
        }
    }

    /* Each quad: a (here), b (one ring down), c (down and one segment on), */
    /* d (one segment on). At the north pole a and d are the same point,    */
    /* so a, c, d is skipped; at the south pole b and c are, so a, b, c is. */
    uint32_t *index = data->indices;
    for (uint32_t ring = 0; ring < rings; ++ring) {
        for (uint32_t segment = 0; segment < segments; ++segment) {
            uint32_t a = ring * columns + segment;
            uint32_t b = a + columns;
            uint32_t c = b + 1;
            uint32_t d = a + 1;

            if (ring != rings - 1) {
                *index++ = a;
                *index++ = b;
                *index++ = c;
            }
            if (ring != 0) {
                *index++ = a;
                *index++ = c;
                *index++ = d;
            }
        }
    }

    return true;
}

/* ------------------------------------------------------------------------ */
/* Freeing                                                                   */
/* ------------------------------------------------------------------------ */

void at_mesh_data_free(at_mesh_data *data) {
    if (!data) {
        return;
    }

    free(data->vertices);
    free(data->indices);
    memset(data, 0, sizeof(*data));
}