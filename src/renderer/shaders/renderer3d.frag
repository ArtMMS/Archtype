#version 450

// Engine shader for the 3D renderer: compiled into the library at build time.

layout(set = 0, binding = 0) uniform Frame {
    mat4 view_projection;
    vec4 camera_position;
    vec4 light_direction;
    vec4 light_color;
    vec4 ambient_sky;
    vec4 ambient_ground;
} frame;

layout(push_constant) uniform Object {
    mat4 model;
    vec4 normal_matrix[3];
    vec4 color;
} object;

layout(location = 0) in vec3 in_world_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

// Colors are given in sRGB (like a color picker), but lighting works in
// linear space; the sRGB swapchain converts the result back.
vec3 srgb_to_linear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

void main() {
    vec3 normal = normalize(in_normal);
    vec3 base   = srgb_to_linear(object.color.rgb);

    // Ambient: from the sky on faces pointing up, from the ground on faces
    // pointing down, blended in between
    vec3 ambient = mix(frame.ambient_ground.rgb, frame.ambient_sky.rgb, 0.5 + 0.5 * normal.y);

    // The sun: brightest on faces turned toward it
    float diffuse = max(dot(normal, -normalize(frame.light_direction.xyz)), 0.0);

    out_color = vec4(base * (ambient + frame.light_color.rgb * diffuse), 1.0);
}