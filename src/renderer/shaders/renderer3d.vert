#version 450

// Engine shader for the 3D renderer: compiled into the library at build time.

// Set 0: shared by every mesh of a pass
layout(set = 0, binding = 0) uniform Frame {
    mat4 view_projection;
    vec4 camera_position; // xyz
    vec4 light_direction; // xyz: where the sun light travels
    vec4 light_color;     // rgb, linear
    vec4 ambient_sky;     // rgb, linear: light on faces pointing up
    vec4 ambient_ground;  // rgb, linear: light on faces pointing down
} frame;

// Per mesh
layout(push_constant) uniform Object {
    mat4 model;
    vec4 normal_matrix[3]; // Columns of the inverse transpose of the model
    vec4 color;            // sRGB
} object;

// at_vertex3d (see at_mesh_vertex_layout)
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec3 out_world_position;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec2 out_uv;

void main() {
    vec4 world         = object.model * vec4(in_position, 1.0);
    mat3 normal_matrix = mat3(object.normal_matrix[0].xyz,
                              object.normal_matrix[1].xyz,
                              object.normal_matrix[2].xyz);

    out_world_position = world.xyz;
    out_normal         = normal_matrix * in_normal;
    out_uv             = in_uv;
    gl_Position        = frame.view_projection * world;
}