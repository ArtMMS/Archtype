#version 450

// Set once per frame by the application (uniform buffer, binding 0)
layout(set = 0, binding = 0) uniform Scene {
    mat4 view_projection;
    vec4 camera_position; // xyz
    vec4 fog_color;       // rgb (sRGB), a = fog density
    vec4 light_direction; // xyz: where the light travels to
} scene;

// Set before each draw (push constants)
layout(push_constant) uniform Object {
    mat4 model;
    vec4 color; // rgb (sRGB), a = strength of the floor checkerboard
} object;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

layout(location = 0) out vec3 out_world_position;
layout(location = 1) out vec3 out_normal;

void main() {
    vec4 world = object.model * vec4(in_position, 1.0);

    out_world_position = world.xyz;
    out_normal         = mat3(object.model) * in_normal; // Fine for these boxes
    gl_Position        = scene.view_projection * world;
}