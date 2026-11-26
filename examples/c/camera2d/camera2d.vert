#version 450

// Set once per frame by the application (uniform buffer, binding 0)
layout(set = 0, binding = 0) uniform Camera {
    mat4 view_projection;
} camera;

// World position: pixels at zoom 1, +Y up
layout(location = 0) in vec2 in_position;
layout(location = 1) in vec3 in_color;

layout(location = 0) out vec3 out_color;

void main() {
    gl_Position = camera.view_projection * vec4(in_position, 0.0, 1.0);
    out_color   = in_color;
}