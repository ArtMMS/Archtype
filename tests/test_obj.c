/* Tests for the OBJ/MTL loader and the mesh tools that don't need a GPU: */
/* parsing, vertex merging, triangulation, parts and materials, and the   */
/* generated normals and tangents.                                        */

#include <math.h>
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

static bool near(float a, float b, float epsilon) {
    return fabsf(a - b) <= epsilon;
}

static bool near3(const float *v, float x, float y, float z, float epsilon) {
    return near(v[0], x, epsilon) && near(v[1], y, epsilon) && near(v[2], z, epsilon);
}

static float dot3(const float *a, const float *b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static float length3(const float *v) {
    return sqrtf(dot3(v, v));
}

static bool parse(at_model_data *data, const char *obj, const char *mtl) {
    return at_model_data_parse_obj(data, obj, strlen(obj), mtl, mtl ? strlen(mtl) : 0);
}

/* ------------------------------------------------------------------------ */
/* Checks shared by the tests                                                */
/* ------------------------------------------------------------------------ */

/* Every index valid, normals and tangents unit length and perpendicular, */
/* w of 1 or -1                                                           */
static void check_mesh(const at_mesh_data *mesh) {
    bool indices_ok  = mesh->index_count % 3 == 0;
    bool normals_ok  = true;
    bool tangents_ok = true;

    for (uint32_t i = 0; i < mesh->index_count; ++i) {
        indices_ok = indices_ok && mesh->indices[i] < mesh->vertex_count;
    }
    for (uint32_t i = 0; i < mesh->vertex_count; ++i) {
        const at_vertex3d *v = &mesh->vertices[i];
        normals_ok  = normals_ok && near(length3(v->normal), 1.0f, 1e-3f);
        tangents_ok = tangents_ok && near(length3(v->tangent), 1.0f, 1e-3f)
                   && fabsf(dot3(v->tangent, v->normal)) < 1e-3f
                   && (v->tangent[3] == 1.0f || v->tangent[3] == -1.0f);
    }

    CHECK(indices_ok);
    CHECK(normals_ok);
    CHECK(tangents_ok);
}

/* For every triangle whose UVs span it: the tangent goes along +u, and */
/* cross(normal, tangent) * w goes up in the image (-v)                 */
static void check_tangent_directions(const at_mesh_data *mesh) {
    bool along_u  = true;
    bool up_image = true;

    for (uint32_t i = 0; i + 2 < mesh->index_count; i += 3) {
        const at_vertex3d *v0 = &mesh->vertices[mesh->indices[i]];
        const at_vertex3d *v1 = &mesh->vertices[mesh->indices[i + 1]];
        const at_vertex3d *v2 = &mesh->vertices[mesh->indices[i + 2]];

        float e1[3], e2[3], e3[3];
        for (int k = 0; k < 3; ++k) {
            e1[k] = v1->position[k] - v0->position[k];
            e2[k] = v2->position[k] - v0->position[k];
            e3[k] = v2->position[k] - v1->position[k];
        }

        /* Triangles with (almost) no area, like the ones at a sphere's */
        /* poles, give no direction                                     */
        if (length3(e1) < 1e-4f || length3(e2) < 1e-4f || length3(e3) < 1e-4f) {
            continue;
        }

        float du1 = v1->uv[0] - v0->uv[0], dv1 = v1->uv[1] - v0->uv[1];
        float du2 = v2->uv[0] - v0->uv[0], dv2 = v2->uv[1] - v0->uv[1];
        float det = du1 * dv2 - du2 * dv1;
        if (fabsf(det) < 1e-8f) {
            continue;
        }

        float u_dir[3], v_dir[3];
        for (int k = 0; k < 3; ++k) {
            u_dir[k] = (e1[k] * dv2 - e2[k] * dv1) / det;
            v_dir[k] = (e2[k] * du1 - e1[k] * du2) / det;
        }

        const at_vertex3d *corners[3] = { v0, v1, v2 };
        for (int c = 0; c < 3; ++c) {
            const float *n = corners[c]->normal;
            const float *t = corners[c]->tangent;
            float b[3] = { (n[1] * t[2] - n[2] * t[1]) * t[3],
                           (n[2] * t[0] - n[0] * t[2]) * t[3],
                           (n[0] * t[1] - n[1] * t[0]) * t[3] };
            along_u  = along_u && dot3(t, u_dir) > 0.0f;
            up_image = up_image && dot3(b, v_dir) < 0.0f;
        }
    }

    CHECK(along_u);
    CHECK(up_image);
}

/* ------------------------------------------------------------------------ */
/* OBJ                                                                       */
/* ------------------------------------------------------------------------ */

/* A quad in the XY plane facing +Z, with UVs and normals */
static const char *k_quad_obj =
    "# A quad\n"
    "v -1 -1 0\n"
    "v  1 -1 0\n"
    "v  1  1 0\n"
    "v -1  1 0\n"
    "vt 0 0\n"
    "vt 1 0\n"
    "vt 1 1\n"
    "vt 0 1\n"
    "vn 0 0 1\n"
    "f 1/1/1 2/2/1 3/3/1 4/4/1\n";

static void test_quad(void) {
    at_model_data data;
    CHECK(parse(&data, k_quad_obj, NULL));
    CHECK(data.part_count == 1);
    if (data.part_count != 1) {
        at_model_data_free(&data);
        return;
    }

    const at_mesh_data *mesh = &data.parts[0].mesh;

    /* One quad: 2 triangles sharing 4 vertices */
    CHECK(mesh->vertex_count == 4);
    CHECK(mesh->index_count == 6);
    if (mesh->vertex_count == 4 && mesh->index_count == 6) {
        /* Fan from the first corner, order kept (counter-clockwise) */
        CHECK(mesh->indices[0] == 0 && mesh->indices[1] == 1 && mesh->indices[2] == 2);
        CHECK(mesh->indices[3] == 0 && mesh->indices[4] == 2 && mesh->indices[5] == 3);

        /* v is flipped: OBJ (0,0) is the bottom-left, the engine's the top-left */
        CHECK(near(mesh->vertices[0].uv[0], 0.0f, 1e-6f));
        CHECK(near(mesh->vertices[0].uv[1], 1.0f, 1e-6f));
        CHECK(near(mesh->vertices[2].uv[1], 0.0f, 1e-6f));

        CHECK(near3(mesh->vertices[1].position, 1.0f, -1.0f, 0.0f, 1e-6f));
        CHECK(near3(mesh->vertices[3].normal, 0.0f, 0.0f, 1.0f, 1e-6f));

        /* Tangent to +X (u grows to the right), image up is +Y: w = 1 */
        CHECK(near3(mesh->vertices[0].tangent, 1.0f, 0.0f, 0.0f, 1e-5f));
        CHECK(mesh->vertices[0].tangent[3] == 1.0f);
        check_mesh(mesh);
        check_tangent_directions(mesh);
    }

    /* No mtllib: one default material */
    CHECK(data.material_count == 1);
    if (data.material_count == 1) {
        CHECK(strcmp(data.materials[0].name, "default") == 0);
        CHECK(data.parts[0].material == 0);
    }

    at_model_data_free(&data);
    CHECK(data.parts == NULL && data.part_count == 0);
}

/* A pentagon, a negative-index face, CRLF line endings, extra values */
static void test_polygons_and_indices(void) {
    const char *obj =
        "v 0 0 0 1.0\r\n"           /* w is ignored */
        "v 2 0 0 0.5 0.5 0.5\r\n"   /* vertex colors are ignored */
        "v 3 2 0\r\n"
        "v 1 3 0\r\n"
        "v -1 2 0\r\n"
        "f 1 2 3 4 5\r\n"
        "f -5 -4 -3\r\n";           /* The same as f 1 2 3 */

    at_model_data data;
    CHECK(parse(&data, obj, NULL));
    if (data.part_count == 1) {
        const at_mesh_data *mesh = &data.parts[0].mesh;
        CHECK(mesh->vertex_count == 5);     /* Corners merged: 5 positions */
        CHECK(mesh->index_count == 3 * 4);  /* Pentagon: 3, triangle: 1    */
        CHECK(mesh->indices[9] == 0 && mesh->indices[10] == 1 && mesh->indices[11] == 2);

        /* No normals in the file: generated, facing +Z (counter-clockwise) */
        CHECK(near3(mesh->vertices[0].normal, 0.0f, 0.0f, 1.0f, 1e-5f));
        check_mesh(mesh);
    }
    at_model_data_free(&data);
}

/* Objects, groups and materials split the parts */
static void test_parts(void) {
    const char *obj =
        "mtllib ignored.mtl\n"     /* Ignored: the MTL comes as text */
        "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
        "o Box\n"
        "usemtl Red\n"
        "f 1 2 3\n"
        "usemtl Blue\n"
        "f 1 3 4\n"
        "o Lid\n"                  /* Keeps the material: Blue */
        "f 1 2 4\n"
        "usemtl Missing\n"         /* Unknown material: default */
        "f 2 3 4\n"
        "g Empty\n"                /* A group with no faces: no part */
        "s off\n"
        "l 1 2\n";

    const char *mtl =
        "newmtl Red\n"
        "Kd 1 0 0\n"
        "newmtl Blue\n"
        "Kd 0 0 1\n";

    at_model_data data;
    CHECK(parse(&data, obj, mtl));
    CHECK(data.part_count == 4);
    CHECK(data.material_count == 3); /* Red, Blue, default */

    if (data.part_count == 4 && data.material_count == 3) {
        CHECK(strcmp(data.parts[0].name, "Box") == 0);
        CHECK(strcmp(data.parts[1].name, "Box") == 0);
        CHECK(strcmp(data.parts[2].name, "Lid") == 0);
        CHECK(strcmp(data.materials[data.parts[0].material].name, "Red") == 0);
        CHECK(strcmp(data.materials[data.parts[1].material].name, "Blue") == 0);
        CHECK(strcmp(data.materials[data.parts[2].material].name, "Blue") == 0);
        CHECK(strcmp(data.materials[data.parts[3].material].name, "default") == 0);

        /* Each part has only the vertices it uses */
        CHECK(data.parts[0].mesh.vertex_count == 3);
        CHECK(data.parts[0].mesh.index_count == 3);
    }
    at_model_data_free(&data);
}

static void test_mtl(void) {
    const char *obj =
        "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
        "usemtl Wood\n"
        "f 1 2 3\n";

    const char *mtl =
        "# Materials\n"
        "newmtl Wood\n"
        "Ka 1 1 1\n"                                  /* Not used */
        "Kd 0.5 0.5 0.5\n"
        "Ks 0.25 0.5 0.75\n"
        "Ns 64\n"
        "d 0.5\n"
        "map_Kd -s 2 2 1 -clamp on textures/My Wood.png\n"
        "map_Bump -bm 1.0 textures\\wood_normal.png\n"
        "newmtl Glass\n"
        "Kd 1\n"                                      /* One value: grey */
        "Tr 0.25\n"
        "Ke 2 1 0\n"
        "illum 2\n";

    at_model_data data;
    CHECK(parse(&data, obj, mtl));
    CHECK(data.material_count == 2);

    if (data.material_count == 2) {
        const at_model_material_data *wood  = &data.materials[0];
        const at_model_material_data *glass = &data.materials[1];

        CHECK(strcmp(wood->name, "Wood") == 0);
        /* Linear 0.5 is sRGB 0.7354 */
        CHECK(near(wood->material.base_color.r, 0.7354f, 1e-3f));
        CHECK(near(wood->material.base_color.a, 0.5f, 1e-6f));
        CHECK(wood->material.transparent);
        CHECK(near(wood->material.specular, 0.5f, 1e-6f));
        CHECK(near(wood->material.shininess, 64.0f, 1e-6f));
        CHECK(wood->material.base_texture == NULL);
        CHECK(strcmp(wood->base_texture_path, "textures/My Wood.png") == 0);
        CHECK(strcmp(wood->normal_texture_path, "textures/wood_normal.png") == 0);

        CHECK(strcmp(glass->name, "Glass") == 0);
        CHECK(near(glass->material.base_color.g, 1.0f, 1e-6f));
        CHECK(near(glass->material.base_color.a, 0.75f, 1e-6f));
        /* Ke 2 1 0: color (1, 0.5, 0) at strength 2 */
        CHECK(near(glass->material.emissive_strength, 2.0f, 1e-6f));
        CHECK(near(glass->material.emissive.r, 1.0f, 1e-6f));
        CHECK(near(glass->material.emissive.g, 0.7354f, 1e-3f));
        CHECK(glass->base_texture_path[0] == '\0');
    }
    CHECK(data.part_count == 1 && data.parts[0].material == 0);
    at_model_data_free(&data);
}

/* The same quad with v/vt (no normals) and v//vn (no UVs) */
static void test_corner_formats(void) {
    const char *obj =
        "v -1 0 1\nv 1 0 1\nv 1 0 -1\nv -1 0 -1\n"
        "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
        "vn 0 1 0\n"
        "g with_uvs\n"
        "f 1/1 2/2 3/3 4/4\n"
        "g with_normals\n"
        "f 1//1 2//1 3//1 4//1\n";

    at_model_data data;
    CHECK(parse(&data, obj, NULL));
    CHECK(data.part_count == 2);

    if (data.part_count == 2) {
        /* A floor seen from above: generated normals point up */
        const at_mesh_data *uvs = &data.parts[0].mesh;
        CHECK(near3(uvs->vertices[0].normal, 0.0f, 1.0f, 0.0f, 1e-5f));
        check_mesh(uvs);
        check_tangent_directions(uvs);

        /* No UVs: all (0, 0), and still a valid tangent */
        const at_mesh_data *normals = &data.parts[1].mesh;
        CHECK(near(normals->vertices[2].uv[0], 0.0f, 1e-6f));
        CHECK(near(normals->vertices[2].uv[1], 0.0f, 1e-6f));
        check_mesh(normals);
    }
    at_model_data_free(&data);
}

static void test_errors(void) {
    at_model_data data;

    printf("(the next messages about bad OBJ text are expected)\n");
    fflush(stdout);

    /* Index past the positions defined so far */
    CHECK(!parse(&data, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 9\n", NULL));
    CHECK(data.parts == NULL && data.part_count == 0);

    /* Index 0 doesn't exist in OBJ */
    CHECK(!parse(&data, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 0 1 2\n", NULL));

    /* Not a number (a decimal comma, too) */
    CHECK(!parse(&data, "v 0 x 0\n", NULL));
    CHECK(!parse(&data, "v 0,5 0 0\n", NULL));

    /* A bad corner */
    CHECK(!parse(&data, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1/a 2 3\n", NULL));

    /* No faces */
    CHECK(!parse(&data, "v 0 0 0\n", NULL));
    CHECK(!parse(&data, "", NULL));

    /* Numbers in every notation */
    CHECK(parse(&data, "v -1.5e0 +2 .25\nv 1E1 0. -0\nv 3 2.5e-1 1\nf 1 2 3\n", NULL));
    if (data.part_count == 1) {
        CHECK(near3(data.parts[0].mesh.vertices[0].position, -1.5f, 2.0f, 0.25f, 1e-6f));
        CHECK(near3(data.parts[0].mesh.vertices[1].position, 10.0f, 0.0f, 0.0f, 1e-6f));
        CHECK(near3(data.parts[0].mesh.vertices[2].position, 3.0f, 0.25f, 1.0f, 1e-6f));
    }
    at_model_data_free(&data);

    /* Freeing twice or freeing NULL is allowed */
    at_model_data_free(&data);
    at_model_data_free(NULL);
}

/* ------------------------------------------------------------------------ */
/* Mesh tools on the engine's own meshes                                     */
/* ------------------------------------------------------------------------ */

static void test_compute_on_generated_meshes(void) {
    /* Sphere: recomputed normals match the original ones */
    at_mesh_data sphere;
    CHECK(at_mesh_data_sphere(&sphere, 2.0f, 32, 16));
    if (sphere.vertices) {
        float original[3 * 4096];
        bool  fits = sphere.vertex_count <= 4096;
        CHECK(fits);
        for (uint32_t i = 0; fits && i < sphere.vertex_count; ++i) {
            memcpy(&original[i * 3], sphere.vertices[i].normal, 3 * sizeof(float));
        }

        at_mesh_data_compute_normals(&sphere);
        at_mesh_data_compute_tangents(&sphere);

        bool same = true;
        for (uint32_t i = 0; fits && i < sphere.vertex_count; ++i) {
            same = same && dot3(&original[i * 3], sphere.vertices[i].normal) > 0.99f;
        }
        CHECK(same);
        check_mesh(&sphere);
        check_tangent_directions(&sphere);
        at_mesh_data_free(&sphere);
    }

    /* Cube: recomputed tangents match the generator's (same convention) */
    at_mesh_data cube;
    CHECK(at_mesh_data_cube(&cube, at_vec3_make(1.0f, 2.0f, 3.0f)));
    if (cube.vertices) {
        float original[24 * 4];
        bool  fits = cube.vertex_count <= 24;
        CHECK(fits);
        for (uint32_t i = 0; fits && i < cube.vertex_count; ++i) {
            memcpy(&original[i * 4], cube.vertices[i].tangent, 4 * sizeof(float));
        }

        at_mesh_data_compute_tangents(&cube);

        bool same = true;
        for (uint32_t i = 0; fits && i < cube.vertex_count; ++i) {
            same = same && dot3(&original[i * 4], cube.vertices[i].tangent) > 0.999f
                        && original[i * 4 + 3] == cube.vertices[i].tangent[3];
        }
        CHECK(same);
        check_mesh(&cube);
        check_tangent_directions(&cube);
        at_mesh_data_free(&cube);
    }
}

int main(void) {
    test_quad();
    test_polygons_and_indices();
    test_parts();
    test_mtl();
    test_corner_formats();
    test_errors();
    test_compute_on_generated_meshes();

    printf("%d checks, %d failed\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}