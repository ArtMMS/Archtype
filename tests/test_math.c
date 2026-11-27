/* Tests for the mesh data generators, which don't need a GPU: counts,    */
/* unit normals and tangents, texture coordinates, valid indices, winding */
/* and the tangent space convention.                                      */

#include <math.h>
#include <stdio.h>

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

#define TOLERANCE 1e-4f

static bool near_f(float a, float b) {
    return at_nearly_equalf(a, b, TOLERANCE);
}

static bool near_vec3(at_vec3 a, at_vec3 b) {
    return near_f(a.x, b.x) && near_f(a.y, b.y) && near_f(a.z, b.z);
}

static at_vec3 vec3_of(const float v[3]) {
    return at_vec3_make(v[0], v[1], v[2]);
}

/* cross(normal, tangent) * w: points to the top of the image */
static at_vec3 bitangent_of(const at_vertex3d *vertex) {
    return at_vec3_scale(at_vec3_cross(vec3_of(vertex->normal), vec3_of(vertex->tangent)),
                         vertex->tangent[3]);
}

/* Checks that every mesh must pass. name is printed when one fails. */
static void check_mesh(const char *name, const at_mesh_data *data) {
    int failures_before = s_failures;

    bool normals_ok  = true;
    bool tangents_ok = true;
    bool uvs_ok      = true;

    for (uint32_t i = 0; i < data->vertex_count; ++i) {
        const at_vertex3d *v = &data->vertices[i];
        at_vec3            n = vec3_of(v->normal);
        at_vec3            t = vec3_of(v->tangent);

        if (!near_f(at_vec3_length(n), 1.0f)) {
            normals_ok = false;
        }
        if (!near_f(at_vec3_length(t), 1.0f) || fabsf(at_vec3_dot(n, t)) > TOLERANCE ||
            !near_f(fabsf(v->tangent[3]), 1.0f)) {
            tangents_ok = false;
        }
        if (v->uv[0] < -TOLERANCE || v->uv[0] > 1.0f + TOLERANCE ||
            v->uv[1] < -TOLERANCE || v->uv[1] > 1.0f + TOLERANCE) {
            uvs_ok = false;
        }
    }

    bool indices_ok   = data->index_count % 3 == 0;
    bool winding_ok   = true;
    bool tangent_ok   = true;
    bool bitangent_ok = true;

    for (uint32_t i = 0; i + 2 < data->index_count; i += 3) {
        uint32_t i0 = data->indices[i];
        uint32_t i1 = data->indices[i + 1];
        uint32_t i2 = data->indices[i + 2];
        if (i0 >= data->vertex_count || i1 >= data->vertex_count ||
            i2 >= data->vertex_count) {
            indices_ok = false;
            continue;
        }

        const at_vertex3d *v0 = &data->vertices[i0];
        const at_vertex3d *v1 = &data->vertices[i1];
        const at_vertex3d *v2 = &data->vertices[i2];

        at_vec3 p0 = vec3_of(v0->position);
        at_vec3 e1 = at_vec3_sub(vec3_of(v1->position), p0);
        at_vec3 e2 = at_vec3_sub(vec3_of(v2->position), p0);

        at_vec3 face = at_vec3_cross(e1, e2);
        if (at_vec3_length(face) <= 1e-10f) {
            continue; /* No area: no direction to check */
        }

        /* Counter-clockwise seen from outside: the face normal agrees */
        /* with the vertex normals.                                    */
        at_vec3 normals = at_vec3_add(at_vec3_add(vec3_of(v0->normal), vec3_of(v1->normal)),
                                      vec3_of(v2->normal));
        if (at_vec3_dot(face, normals) <= 0.0f) {
            winding_ok = false;
        }

        /* Where u and v grow on this triangle, from its texture coordinates */
        float du1 = v1->uv[0] - v0->uv[0];
        float dv1 = v1->uv[1] - v0->uv[1];
        float du2 = v2->uv[0] - v0->uv[0];
        float dv2 = v2->uv[1] - v0->uv[1];
        float det = du1 * dv2 - du2 * dv1;
        if (fabsf(det) <= 1e-12f) {
            continue;
        }

        at_vec3 dp_du = at_vec3_scale(at_vec3_sub(at_vec3_scale(e1, dv2),
                                                  at_vec3_scale(e2, dv1)), 1.0f / det);
        at_vec3 dp_dv = at_vec3_scale(at_vec3_sub(at_vec3_scale(e2, du1),
                                                  at_vec3_scale(e1, du2)), 1.0f / det);

        at_vec3 tangents   = at_vec3_add(at_vec3_add(vec3_of(v0->tangent),
                                                     vec3_of(v1->tangent)),
                                         vec3_of(v2->tangent));
        at_vec3 bitangents = at_vec3_add(at_vec3_add(bitangent_of(v0), bitangent_of(v1)),
                                         bitangent_of(v2));

        /* The tangent points where u grows; the bitangent where v shrinks */
        if (at_vec3_dot(tangents, dp_du) <= 0.0f) {
            tangent_ok = false;
        }
        if (at_vec3_dot(bitangents, dp_dv) >= 0.0f) {
            bitangent_ok = false;
        }
    }

    CHECK(data->vertex_count > 0 && data->index_count > 0);
    CHECK(normals_ok);
    CHECK(tangents_ok);
    CHECK(uvs_ok);
    CHECK(indices_ok);
    CHECK(winding_ok);
    CHECK(tangent_ok);
    CHECK(bitangent_ok);

    if (s_failures != failures_before) {
        printf("  (in mesh: %s)\n", name);
    }
}

static void test_cube(void) {
    at_mesh_data cube;
    CHECK(at_mesh_data_cube(&cube, at_vec3_make(2.0f, 3.0f, 4.0f)));
    CHECK(cube.vertex_count == 24 && cube.index_count == 36);
    check_mesh("cube", &cube);

    /* Every vertex is on the surface of the 2 x 3 x 4 box */
    bool inside = true;
    for (uint32_t i = 0; i < cube.vertex_count; ++i) {
        const float *p = cube.vertices[i].position;
        if (fabsf(p[0]) > 1.0f + TOLERANCE || fabsf(p[1]) > 1.5f + TOLERANCE ||
            fabsf(p[2]) > 2.0f + TOLERANCE) {
            inside = false;
        }
    }
    CHECK(inside);
    at_mesh_data_free(&cube);

    /* Freeing empties the data, and a second free is harmless */
    CHECK(cube.vertices == NULL && cube.indices == NULL && cube.vertex_count == 0);
    at_mesh_data_free(&cube);

    /* Invalid size: fails and leaves the data empty */
    at_mesh_data flat;
    CHECK(!at_mesh_data_cube(&flat, at_vec3_make(1.0f, 0.0f, 1.0f)));
    CHECK(flat.vertices == NULL && flat.vertex_count == 0);
}

static void test_plane(void) {
    at_mesh_data plane;
    CHECK(at_mesh_data_plane(&plane, 10.0f, 6.0f, 4, 3));
    CHECK(plane.vertex_count == 5 * 4 && plane.index_count == 4 * 3 * 6);
    check_mesh("plane", &plane);

    /* Flat, facing up */
    bool flat = true;
    for (uint32_t i = 0; i < plane.vertex_count; ++i) {
        if (!near_f(plane.vertices[i].position[1], 0.0f) ||
            !near_vec3(vec3_of(plane.vertices[i].normal), at_vec3_up())) {
            flat = false;
        }
    }
    CHECK(flat);

    /* First vertex: the -X -Z corner, top-left of the image. */
    /* Last vertex: the +X +Z corner, bottom-right.          */
    const at_vertex3d *first = &plane.vertices[0];
    const at_vertex3d *last  = &plane.vertices[plane.vertex_count - 1];
    CHECK(near_vec3(vec3_of(first->position), at_vec3_make(-5.0f, 0.0f, -3.0f)));
    CHECK(near_f(first->uv[0], 0.0f) && near_f(first->uv[1], 0.0f));
    CHECK(near_vec3(vec3_of(last->position), at_vec3_make(5.0f, 0.0f, 3.0f)));
    CHECK(near_f(last->uv[0], 1.0f) && near_f(last->uv[1], 1.0f));
    at_mesh_data_free(&plane);

    /* 0 subdivisions are raised to 1: one square */
    at_mesh_data single;
    CHECK(at_mesh_data_plane(&single, 1.0f, 1.0f, 0, 0));
    CHECK(single.vertex_count == 4 && single.index_count == 6);
    check_mesh("plane 1x1", &single);
    at_mesh_data_free(&single);

    at_mesh_data invalid;
    CHECK(!at_mesh_data_plane(&invalid, -1.0f, 1.0f, 1, 1));
    CHECK(invalid.vertices == NULL);
}

static void test_sphere(void) {
    const uint32_t segments = 16;
    const uint32_t rings    = 8;

    at_mesh_data sphere;
    CHECK(at_mesh_data_sphere(&sphere, 2.0f, segments, rings));
    CHECK(sphere.vertex_count == (segments + 1) * (rings + 1));
    CHECK(sphere.index_count == segments * (rings - 1) * 6);
    check_mesh("sphere", &sphere);

    /* Every vertex is at the radius, with the normal pointing straight out */
    bool round = true;
    for (uint32_t i = 0; i < sphere.vertex_count; ++i) {
        at_vec3 p = vec3_of(sphere.vertices[i].position);
        if (!near_f(at_vec3_length(p), 2.0f) ||
            !near_vec3(at_vec3_scale(p, 0.5f), vec3_of(sphere.vertices[i].normal))) {
            round = false;
        }
    }
    CHECK(round);

    /* North pole first; on the equator, u = 0 is at the back (-Z) */
    /* and u = 0.5 at the front (+Z)                               */
    uint32_t columns = segments + 1;
    CHECK(near_vec3(vec3_of(sphere.vertices[0].position), at_vec3_make(0.0f, 2.0f, 0.0f)));

    const at_vertex3d *back  = &sphere.vertices[(rings / 2) * columns];
    const at_vertex3d *front = &sphere.vertices[(rings / 2) * columns + segments / 2];
    CHECK(near_vec3(vec3_of(back->position), at_vec3_make(0.0f, 0.0f, -2.0f)));
    CHECK(near_f(back->uv[0], 0.0f) && near_f(back->uv[1], 0.5f));
    CHECK(near_vec3(vec3_of(front->position), at_vec3_make(0.0f, 0.0f, 2.0f)));
    CHECK(near_f(front->uv[0], 0.5f));

    /* The seam: the first and last vertex of a ring are in the same */
    /* place, with u = 0 and u = 1                                    */
    const at_vertex3d *seam_start = &sphere.vertices[3 * columns];
    const at_vertex3d *seam_end   = &sphere.vertices[3 * columns + segments];
    CHECK(near_vec3(vec3_of(seam_start->position), vec3_of(seam_end->position)));
    CHECK(near_f(seam_start->uv[0], 0.0f) && near_f(seam_end->uv[0], 1.0f));
    at_mesh_data_free(&sphere);

    /* Too few segments and rings are raised to 3 and 2 */
    at_mesh_data tiny;
    CHECK(at_mesh_data_sphere(&tiny, 1.0f, 1, 1));
    CHECK(tiny.vertex_count == 4 * 3 && tiny.index_count == 3 * 1 * 6);
    check_mesh("sphere 3x2", &tiny);
    at_mesh_data_free(&tiny);

    at_mesh_data invalid;
    CHECK(!at_mesh_data_sphere(&invalid, 0.0f, 16, 8));
    CHECK(invalid.vertices == NULL);
}

static void test_vertex_layout(void) {
    at_vertex_layout layout = at_mesh_vertex_layout();

    CHECK(sizeof(at_vertex3d) == 48);
    CHECK(layout.stride == sizeof(at_vertex3d));
    CHECK(layout.attribute_count == 4);
    CHECK(layout.attributes[0].location == 0 && layout.attributes[0].offset == 0);
    CHECK(layout.attributes[3].location == 3 && layout.attributes[3].offset == 32);
    CHECK(layout.attributes[3].format == AT_VERTEX_FORMAT_FLOAT4);
}

int main(void) {
    test_cube();
    test_plane();
    test_sphere();
    test_vertex_layout();

    if (s_failures > 0) {
        printf("%d of %d checks failed\n", s_failures, s_checks);
        return 1;
    }

    printf("All %d checks passed\n", s_checks);
    return 0;
}