#version 450

layout(set = 0, binding = 0) uniform Camera {
    mat4 view_projection;
} camera;

layout(push_constant) uniform Object {
    mat4 model;
} object;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;

layout(location = 0) out vec2 out_uv;

void main() {
    gl_Position = camera.view_projection * object.model * vec4(in_position, 1.0);
    out_uv      = in_uv;
}