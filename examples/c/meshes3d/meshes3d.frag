#version 450

layout(set = 0, binding = 0) uniform Scene {
    mat4 view_projection;
    vec4 light_direction;
} scene;

layout(push_constant) uniform Object {
    mat4 model;
    vec4 color;
    vec4 params;
} object;

layout(location = 0) in vec3 in_world_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_tangent;

layout(location = 0) out vec4 out_color;

// Colors are given in sRGB; the sRGB swapchain expects linear values
vec3 srgb_to_linear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

// A direction as a color: each axis from -1..1 to 0..1, so +X is red,
// +Y green and +Z blue. Converted so the screen shows exactly these values.
vec3 direction_color(vec3 direction) {
    return srgb_to_linear(normalize(direction) * 0.5 + 0.5);
}

void main() {
    int  mode   = int(object.params.x + 0.5);
    vec3 normal = normalize(in_normal);
    vec3 color;

    if (mode == 1) {
        // Normals
        color = direction_color(normal);
    } else if (mode == 2) {
        // Texture coordinates: an 8 x 8 checkerboard over the whole texture,
        // with red growing along u and green along v
        vec2  cell    = floor(in_uv * 8.0);
        float checker = mod(cell.x + cell.y, 2.0);
        color = srgb_to_linear(vec3(in_uv, 0.25) * (0.55 + 0.45 * checker));
    } else if (mode == 3) {
        // Tangents
        color = direction_color(in_tangent.xyz);
    } else {
        // Simple shading: some light from the sky (more on faces pointing
        // up) and one directional light. Real lighting comes later.
        vec3  base    = srgb_to_linear(object.color.rgb);
        float sky     = 0.5 + 0.5 * normal.y;
        float diffuse = max(dot(normal, -normalize(scene.light_direction.xyz)), 0.0);
        color = base * (0.12 + 0.18 * sky + 0.75 * diffuse);
    }

    out_color = vec4(color, 1.0);
}