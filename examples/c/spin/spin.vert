#version 450

// Set once per frame by the application (uniform buffer, binding 0)
layout(set = 0, binding = 0) uniform Camera {
    mat4 view_projection;
} camera;

// Set before each draw (push constants)
layout(push_constant) uniform Object {
    mat4 model;
    vec4 tint;
} object;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;

layout(location = 0) out vec3 out_color;

void main() {
    gl_Position = camera.view_projection * object.model * vec4(in_position, 1.0);
    out_color   = in_color * object.tint.rgb;
}