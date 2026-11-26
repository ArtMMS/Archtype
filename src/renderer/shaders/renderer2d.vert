#version 450

// Engine shader for the 2D renderer: compiled into the library at build time.

// Camera, set once per batch
layout(push_constant) uniform Camera {
    mat4 view_projection;
} camera;

layout(location = 0) in vec2 in_position; // World space
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;    // sRGB, 0..1

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec4 out_color;

// Colors are given in sRGB (like a color picker), but shaders and blending
// work in linear space: convert once per vertex.
vec3 srgb_to_linear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

void main() {
    gl_Position = camera.view_projection * vec4(in_position, 0.0, 1.0);
    out_uv      = in_uv;
    out_color   = vec4(srgb_to_linear(in_color.rgb), in_color.a);
}