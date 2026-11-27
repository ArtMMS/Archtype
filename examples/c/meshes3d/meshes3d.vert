#version 450

// Set once per frame by the application (uniform buffer, binding 0)
layout(set = 0, binding = 0) uniform Scene {
    mat4 view_projection;
    vec4 light_direction; // xyz: where the light travels to
} scene;

// Set before each draw (push constants)
layout(push_constant) uniform Object {
    mat4 model;
    vec4 color;  // rgb (sRGB)
    vec4 params; // x = view mode
} object;

// at_vertex3d (see at_mesh_vertex_layout)
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_tangent;

layout(location = 0) out vec3 out_world_position;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec2 out_uv;
layout(location = 3) out vec4 out_tangent;

void main() {
    vec4 world = object.model * vec4(in_position, 1.0);

    // Normals use the inverse transpose, so they stay perpendicular to the
    // surface even when the object is scaled more on one axis
    mat3 normal_matrix = transpose(inverse(mat3(object.model)));

    out_world_position = world.xyz;
    out_normal         = normal_matrix * in_normal;
    out_uv             = in_uv;
    out_tangent        = vec4(mat3(object.model) * in_tangent.xyz, in_tangent.w);
    gl_Position        = scene.view_projection * world;
}