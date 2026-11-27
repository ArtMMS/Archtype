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

// Set 1: the material's texture (sRGB textures are read as linear colors)
layout(set = 1, binding = 0) uniform sampler2D base_texture;

layout(push_constant) uniform Object {
    mat4 model;
    vec4 base_color;
    vec4 emissive;
    vec4 params;
    vec4 uv_transform;
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
    float specular     = object.params.x;
    float shininess    = object.params.y;
    float alpha_cutoff = object.params.z;
    bool  unlit        = object.params.w > 0.5;

    // Surface color: the material color times its texture
    vec4 base = vec4(srgb_to_linear(object.base_color.rgb), object.base_color.a)
              * texture(base_texture, in_uv);

    // Holes (fences, leaves): these pixels are not drawn at all
    if (base.a < alpha_cutoff) {
        discard;
    }

    vec3 emissive = srgb_to_linear(object.emissive.rgb) * object.emissive.w;

    if (unlit) {
        out_color = vec4(base.rgb + emissive, base.a);
        return;
    }

    // Double-sided materials: the back face is lit as a surface facing us
    vec3 normal = normalize(in_normal);
    if (!gl_FrontFacing) {
        normal = -normal;
    }

    // Ambient: from the sky on faces pointing up, from the ground on faces
    // pointing down, blended in between
    vec3 ambient = mix(frame.ambient_ground.rgb, frame.ambient_sky.rgb, 0.5 + 0.5 * normal.y);

    // The sun: diffuse light, brightest on faces turned toward it, and a
    // Blinn-Phong highlight around the direction halfway between the light
    // and the camera
    vec3  to_light  = -normalize(frame.light_direction.xyz);
    vec3  to_camera = normalize(frame.camera_position.xyz - in_world_position);
    vec3  halfway   = normalize(to_light + to_camera);
    float diffuse   = max(dot(normal, to_light), 0.0);
    float highlight = diffuse > 0.0 ? pow(max(dot(normal, halfway), 0.0), shininess) : 0.0;

    vec3 color = base.rgb * (ambient + frame.light_color.rgb * diffuse)
               + frame.light_color.rgb * highlight * specular
               + emissive;

    out_color = vec4(color, base.a);
}