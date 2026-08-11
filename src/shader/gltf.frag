#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 world_position;
layout(location = 1) in vec3 world_normal;
layout(location = 2) in vec2 texcoord0;
layout(location = 3) in vec2 texcoord1;
layout(location = 4) flat in uint material_index;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform texture2D sampled_images[];
layout(set = 0, binding = 1) uniform sampler samplers[];

struct MaterialRow {
    vec4 base_color;
    vec4 emissive_metallic;
    vec4 roughness_alpha;
    vec4 texture_scales;
    uvec4 image_slots;
    uvec4 sampler_slots;
    uvec4 texcoords;
    uvec4 emissive_texture;
};

layout(set = 0, binding = 4, std430) readonly buffer MaterialTable {
    MaterialRow rows[];
} material_tables[];

layout(push_constant) uniform ObjectPush {
    uint frame_uniform_slot;
    uint transform_buffer_slot;
    uint material_buffer_slot;
} object_push;

const float PI = 3.14159265359;

vec2 material_uv(uint set_index)
{
    return set_index == 1 ? texcoord1 : texcoord0;
}

void main()
{
    uint table_slot = nonuniformEXT(object_push.material_buffer_slot);
    MaterialRow material = material_tables[table_slot].rows[material_index];
    uint base_image = nonuniformEXT(material.image_slots.x);
    uint base_sampler = nonuniformEXT(material.sampler_slots.x);
    vec4 base_color = material.base_color * texture(sampler2D(sampled_images[base_image], samplers[base_sampler]),
                                                     material_uv(material.texcoords.x));
    if (material.roughness_alpha.z == 1.0 && base_color.a < material.roughness_alpha.y) discard;

    uint normal_image = nonuniformEXT(material.image_slots.z);
    uint normal_sampler = nonuniformEXT(material.sampler_slots.z);
    vec3 tangent_normal = texture(sampler2D(sampled_images[normal_image], samplers[normal_sampler]),
                                  material_uv(material.texcoords.z)).xyz * 2.0 - 1.0;
    tangent_normal.xy *= material.texture_scales.x;
    vec3 dp1 = dFdx(world_position);
    vec3 dp2 = dFdy(world_position);
    vec2 duv1 = dFdx(material_uv(material.texcoords.z));
    vec2 duv2 = dFdy(material_uv(material.texcoords.z));
    vec3 base_normal = normalize(world_normal);
    vec3 tangent_raw = dp1 * duv2.y - dp2 * duv1.y;
    vec3 bitangent_raw = -dp1 * duv2.x + dp2 * duv1.x;
    vec3 normal = base_normal;
    if (dot(tangent_raw, tangent_raw) > 1e-10 && dot(bitangent_raw, bitangent_raw) > 1e-10)
        normal = normalize(mat3(normalize(tangent_raw), normalize(bitangent_raw), base_normal) * tangent_normal);
    vec3 light_direction = normalize(vec3(-0.4, -1.0, -0.3));
    vec3 view_direction = normalize(-world_position);
    vec3 half_vector = normalize(view_direction - light_direction);
    uint mr_image = nonuniformEXT(material.image_slots.y);
    uint mr_sampler = nonuniformEXT(material.sampler_slots.y);
    vec4 mr_sample = texture(sampler2D(sampled_images[mr_image], samplers[mr_sampler]), material_uv(material.texcoords.y));
    float metallic = clamp(material.emissive_metallic.w * mr_sample.b, 0.0, 1.0);
    float roughness = clamp(material.roughness_alpha.x * mr_sample.g, 0.04, 1.0);
    float ndotl = max(dot(normal, -light_direction), 0.0);
    float ndoth = max(dot(normal, half_vector), 0.0);
    vec3 f0 = mix(vec3(0.04), base_color.rgb, metallic);
    vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - max(dot(half_vector, view_direction), 0.0), 5.0);
    float shininess = mix(256.0, 2.0, roughness);
    vec3 specular = fresnel * pow(ndoth, shininess) * ndotl;
    vec3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * base_color.rgb / PI;
    uint occlusion_image = nonuniformEXT(material.image_slots.w);
    uint occlusion_sampler = nonuniformEXT(material.sampler_slots.w);
    float sampled_occlusion = texture(sampler2D(sampled_images[occlusion_image], samplers[occlusion_sampler]),
                                      material_uv(material.texcoords.w)).r;
    float occlusion = mix(1.0, sampled_occlusion, material.texture_scales.y);
    uint emissive_image = nonuniformEXT(material.emissive_texture.x);
    uint emissive_sampler = nonuniformEXT(material.emissive_texture.y);
    vec3 emissive = material.emissive_metallic.xyz *
                    texture(sampler2D(sampled_images[emissive_image], samplers[emissive_sampler]),
                            material_uv(material.emissive_texture.z)).rgb;
    vec3 ambient = base_color.rgb * 0.03 * occlusion;
    vec3 color = ambient + (diffuse + specular) * ndotl * 3.0 + emissive;
    out_color = vec4(color, base_color.a);
}
